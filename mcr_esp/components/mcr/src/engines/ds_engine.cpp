/*
          mcrDS - Master Control Remote Dallas Semiconductor
          Copyright (C) 2017  Tim Hughey

          This program is free software: you can redistribute it and/or modify
          it under the terms of the GNU General Public License as published by
          the Free Software Foundation, either version 3 of the License, or
          (at your option) any later version.

          This program is distributed in the hope that it will be useful,
          but WITHOUT ANY WARRANTY; without even the implied warranty of
          MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
          GNU General Public License for more details.

          You should have received a copy of the GNU General Public License
          along with this program.  If not, see <http://www.gnu.org/licenses/>.

          https://www.wisslanding.com
      */

#include <bitset>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include "cmds/cmd_queues.hpp"
#include "cmds/cmd_switch.hpp"
#include "devs/base.hpp"
#include "devs/ds_dev.hpp"
#include "drivers/owb.h"
#include "drivers/owb_gpio.h"
#include "engines/ds_engine.hpp"
#include "engines/engine.hpp"
#include "misc/elapsedMillis.hpp"
#include "misc/mcr_types.hpp"
#include "net/mcr_net.hpp"
#include "protocols/mqtt.hpp"

namespace mcr {

mcrDS_t *__singleton__ = nullptr;

mcrDS::mcrDS() {
  setTags(localTags());
  // setLoggingLevel(ESP_LOG_DEBUG);
  // setLoggingLevel(ESP_LOG_INFO);
  setLoggingLevel(ESP_LOG_WARN);
  // setLoggingLevel(tagConvert(), ESP_LOG_INFO);
  // setLoggingLevel(tagReport(), ESP_LOG_INFO);
  // setLoggingLevel(tagDiscover(), ESP_LOG_INFO);
  // setLoggingLevel(tagCommand(), ESP_LOG_INFO);
  // setLoggingLevel(tagSetDS2408(), ESP_LOG_INFO);

  // task setup
  _engine_task_name = tagEngine();
  _engine_stack_size = 5 * 1024;
  _engine_priority = CONFIG_MCR_DS_TASK_INIT_PRIORITY;
}

bool mcrDS::checkDevicesPowered() {
  bool rc = false;
  bool present = false;
  owb_status owb_s;
  uint8_t read_pwr_cmd[] = {0xcc, 0xb4};
  uint8_t pwr = 0x00;

  // reset the bus before and after the powered check since this
  // method may be called in conjuction of other bus operations
  owb_reset(_ds, &present);

  owb_s = owb_write_bytes(_ds, read_pwr_cmd, sizeof(read_pwr_cmd));
  owb_s = owb_read_byte(_ds, &pwr);

  if ((owb_s == OWB_STATUS_OK) && pwr) {
    ESP_LOGI(tagDiscover(), "all devices are powered");
    rc = true;
  } else {
    ESP_LOGW(tagDiscover(),
             "at least one device is not powered (or err owb_s=%d)", owb_s);
  }

  owb_reset(_ds, &present);

  return rc;
}

void mcrDS::command(void *task_data) {
  // no setup required before jumping into task loop

  for (;;) {
    BaseType_t queue_rc = pdFALSE;
    mcrCmdSwitch_t *cmd = nullptr;

    clearNeedBus();
    queue_rc = xQueueReceive(_cmd_q, &cmd, portMAX_DELAY);
    elapsedMicros process_cmd;

    if (queue_rc == pdFALSE) {
      ESP_LOGW(tagCommand(), "[rc=%d] queue receive failed", queue_rc);

      // HACK: pause if the queue receive fails
      // the intent here is to give time for vTaskDelete to complete
      vTaskDelay(pdMS_TO_TICKS(20000));
      continue;
    }

    ESP_LOGD(tagCommand(), "processing %s", cmd->debug().get());

    dsDev_t *dev = findDevice(cmd->dev_id());

    if ((dev != nullptr) && dev->isValid()) {
      bool set_rc = false;
      bool ack_success = false;
      bool remote_log = false;

      trackSwitchCmd(true);

      needBus();
      ESP_LOGD(tagCommand(), "attempting to aquire bux mutex...");
      elapsedMicros bus_wait;
      takeBus();

      if (bus_wait < 500) {
        ESP_LOGD(tagCommand(), "acquired bus mutex (%lluus)",
                 (uint64_t)bus_wait);
      } else {
        ESP_LOGW(tagCommand(), "acquire bus mutex took %0.2fms",
                 (float)(bus_wait / 1000.0));
      }

      if (dev->isDS2406()) {
        set_rc = setDS2406(*cmd, dev);
      } else if (dev->isDS2408()) {
        set_rc = setDS2408(*cmd, dev);
      } else if (dev->isDS2413()) {
        set_rc = setDS2413(*cmd, dev);
      }

      if (set_rc) {
        ack_success = commandAck(*cmd);
      }

      trackSwitchCmd(false);

      // we create a textReading then wrap in textReading_ptr_t (aka unique_ptr)
      // to delete when it falls out of scope
      textReading_t *rlog(new textReading_t);
      textReading_ptr_t rlog_ptr(rlog);

      if (set_rc && ack_success) {
        if (remote_log) {
          rlog->printf("cmd and ack complete for %s",
                       (const char *)cmd->dev_id().c_str());
        }
        ESP_LOGD(tagCommand(), "%s", rlog->text());

      } else {
        remote_log = true;
        rlog->printf("%s ack failed set_rc(%s) ack(%s)",
                     (const char *)cmd->dev_id().c_str(),
                     (set_rc) ? "true" : "false",
                     (ack_success) ? "true" : "false");
        ESP_LOGW(tagCommand(), "%s", rlog->text());
      }

      rlog->publish();

      giveBus();

      ESP_LOGD(tagCommand(), "released bus mutex");
    } else {
      ESP_LOGD(tagCommand(), "device %s not available",
               (const char *)cmd->dev_id().c_str());
    }

    if (process_cmd > 100000) { // 100ms
      ESP_LOGW(tagCommand(), "took %0.3fms for %s",
               (float)(process_cmd / 1000.0), cmd->debug().get());
    }

    delete cmd;
  }
} // namespace mcr

bool mcrDS::commandAck(mcrCmdSwitch_t &cmd) {
  bool rc = true;
  int64_t start = esp_timer_get_time();
  dsDev_t *dev = findDevice(cmd.dev_id());

  if (dev != nullptr) {
    rc = readDevice(dev);

    if (rc && cmd.ack()) {
      setCmdAck(cmd);
      publish(cmd);
    }
  } else {
    ESP_LOGW(tagCommand(), "unable to find device for cmd ack %s",
             cmd.debug().get());
  }

  ESP_LOGI(tagCommand(), "completed cmd: %s", cmd.debug().get());

  int64_t elapsed_us = esp_timer_get_time() - start;
  if (elapsed_us > 100000) { // 100ms
    float elapsed_ms = (float)(elapsed_us / 1000.0);
    ESP_LOGW(tagCommand(), "ACK took %0.3fms", elapsed_ms);
  }

  return rc;
}

void mcrDS::convert(void *task_data) {
  uint8_t temp_convert_cmd[] = {0xcc, 0x44};

  // ensure the temp available bit is cleared at task startup
  tempUnavailable();

  for (;;) {
    // bool temp_convert_done = false;
    bool present = false;
    uint8_t data = 0x00;
    owb_status owb_s;

    // wait here for devices available bit
    waitFor(devicesOrTempSensorsBit());

    // now that the wait has been satisified record the last wake time
    // this is important to avoid performing the convert too frequently
    _convertTask.lastWake = xTaskGetTickCount();

    // use event bits to signal when there are temperatures available
    // start by clearing the bit to signal there isn't a temperature available
    tempUnavailable();

    trackConvert(true);

    if (!devicesPowered() && !tempDevicesPresent()) {
      ESP_LOGW(tagConvert(),
               "devices not powered or no temperature devices present");
      trackConvert(false);
      vTaskDelayUntil(&(_convertTask.lastWake), _convert_frequency);
      continue;
    }

    takeBus();

    owb_s = owb_reset(_ds, &present);

    if ((owb_s != OWB_STATUS_OK) && (!present)) {
      ESP_LOGW(tagConvert(), "no devices present (but there should be)");
      giveBus();
      vTaskDelayUntil(&(_convertTask.lastWake), _convert_frequency);
      continue;
    }

    owb_reset(_ds, &present);
    owb_s = owb_write_bytes(_ds, temp_convert_cmd, sizeof(temp_convert_cmd));
    owb_s = owb_read_byte(_ds, &data);

    // before dropping into waiting for the temperature conversion to
    // complete let's double check there weren't any errors after initiating
    // the convert.  additionally, we should see zeroes on the bus since
    // devices will hold the bus low during the convert
    if ((owb_s != OWB_STATUS_OK) || (data != 0x00)) {
      trackConvert(false);
      if (owb_s != OWB_STATUS_OK) {
        ESP_LOGW(tagConvert(), "cmd failed owb_s=%d data=0x%x", owb_s, data);
      }

      if (data == 0xff) {
        ESP_LOGW(tagConvert(), "appears no temperature devices on bus");
      }

      giveBus();
      vTaskDelayUntil(&(_convertTask.lastWake), _convert_frequency);
      continue;
    }

    ESP_LOGD(tagConvert(), "in-progress");

    bool in_progress = true;
    bool temp_available = false;
    uint64_t _wait_start = esp_timer_get_time();
    while ((owb_s == OWB_STATUS_OK) && in_progress) {
      owb_s = owb_read_byte(_ds, &data);

      if (owb_s != OWB_STATUS_OK) {
        ESP_LOGW(tagConvert(), "temp convert failed (0x%02x)", owb_s);
        break;
      }

      // if the bus isn't low then the convert is finished
      if (data > 0x00) {
        // NOTE: use a flag here so we can exit the while loop before
        // setting the event group bit since tasks waiting for the bit
        // will immediately wake up.  this allows for clean tracking of
        // the temp convert elapsed time.
        temp_available = true;
        in_progress = false;
      }

      if (in_progress) {
        BaseType_t notified = pdFALSE;

        // wait for time to pass or to be notified that another
        // task needs the bus
        // if the bit was set then clear it
        EventBits_t bits = waitFor(needBusBit(), _temp_convert_wait, true);
        notified = (bits & needBusBit());

        // another task needs the bus so break out of the loop
        if (notified) {
          owb_reset(_ds, &present); // abort the temperature convert
          in_progress = false;      // signal to break from loop
          ESP_LOGW(tagConvert(), "another task needs the bus, convert aborted");
          _convertTask.lastWake -= 5; // NOTE: magic!
        }

        if ((esp_timer_get_time() - _wait_start) >= _max_temp_convert_us) {
          ESP_LOGW(tagConvert(), "temp convert timed out");
          owb_reset(_ds, &present);
          in_progress = false; // signal to break from loop
        }
      }
    }

    giveBus();
    trackConvert(false);

    // signal to other tasks if temperatures are available
    if (temp_available) {
      tempAvailable();
    }

    vTaskDelayUntil(&(_convertTask.lastWake), _convert_frequency);
  }
}

void mcrDS::discover(void *task_data) {
  waitFor(engineBit());
  devicesUnavailable(); // ensure the devices available bit is cleared on task
                        // startup

  for (;;) {
    owb_status owb_s;
    bool present = false;
    bool found = false;
    auto device_found = false;
    auto temp_devices = false;
    bool bus_needed = false;
    OneWireBus_SearchState search_state;

    _discoverTask.lastWake = xTaskGetTickCount();
    bzero(&search_state, sizeof(OneWireBus_SearchState));

    // take the bus before beginning time tracking to avoid
    // artificially inflating discover elapsed time
    takeBus();

    trackDiscover(true);

    owb_s = owb_reset(_ds, &present);

    if (!present) {
      ESP_LOGI(tagDiscover(), "no devices present");
      giveBus();
      trackDiscover(false);
      vTaskDelayUntil(&(_discoverTask.lastWake), _discover_frequency);
      continue;
    }

    owb_s = owb_search_first(_ds, &search_state, &found);

    if (owb_s != OWB_STATUS_OK) {
      ESP_LOGW(tagDiscover(), "search first failed owb_s=%d", owb_s);
      giveBus();
      trackDiscover(false);
      vTaskDelayUntil(&(_discoverTask.lastWake), _discover_frequency);
      continue;
    }

    bool hold_bus = true;
    while ((owb_s == OWB_STATUS_OK) && found && hold_bus) {
      device_found = true;

      mcrDevAddr_t found_addr(search_state.rom_code.bytes, 8);
      dsDev_t dev(found_addr, true);

      if (justSeenDevice(dev)) {
        ESP_LOGD(tagDiscover(), "previously seen %s", dev.debug().get());
      } else {
        dsDev_t *new_dev = new dsDev(dev);
        ESP_LOGI(tagDiscover(), "new (%p) %s", (void *)new_dev,
                 dev.debug().get());
        addDevice(new_dev);
      }

      if (dev.hasTemperature()) {
        temp_devices = true;
      }

      bus_needed = isBusNeeded();

      // another task needs the bus so break out of the loop
      if (bus_needed) {
        ESP_LOGW(tagConvert(), "another task needs the bus, discover aborted");

        owb_reset(_ds, &present);    // abort the search
        hold_bus = false;            // signal to break from loop
        _discoverTask.lastWake -= 5; // NOTE: magic!
      } else {
        // keeping searching
        owb_s = owb_search_next(_ds, &search_state, &found);

        if (owb_s != OWB_STATUS_OK) {
          ESP_LOGW(tagDiscover(), "search next failed owb_s=%d", owb_s);
        }
      }
    }

    // TODO: create specific logic to detect pwr status of each family code
    // ds->reset();
    // ds->select(addr);
    // ds->write(0xB4); // Read Power Supply
    // uint8_t pwr = ds->read_bit();

    _devices_powered = checkDevicesPowered();

    giveBus();
    trackDiscover(false);

    // must set before setting devices_available
    _temp_devices_present = temp_devices;
    temperatureSensors(temp_devices);

    // signal to other tasks if there are devices available
    devicesAvailable(device_found);

    vTaskDelayUntil(&(_discoverTask.lastWake), _discover_frequency);
  }
}

mcrDS_t *mcrDS::instance() {
  if (__singleton__ == nullptr) {
    __singleton__ = new mcrDS();
  }

  return __singleton__;
}

void mcrDS::report(void *task_data) {

  for (;;) {
    // let's wait here for the signal devices are available
    // important to ensure we don't start reporting before
    // the rest of the system is fully available (e.g. wifi, mqtt)
    waitFor(devicesAvailableBit());

    // there are two cases of when report should run:
    //  a. wait for a temperature if there are temperature devices
    //  b. wait a preset duration

    // case a:  wait for temperature to be available
    if (_temp_devices_present) {
      // let's wait here for the temperature available bit
      // once we see it then clear it to ensure we don't run again until
      // it's available again
      ESP_LOGI(tagReport(), "standing by for temperature");
      waitFor(temperatureAvailableBit(), _report_frequency, true);
    }

    // last wake is actually after the event group has been satisified
    _reportTask.lastWake = xTaskGetTickCount();

    trackReport(true);
    ESP_LOGI(tagReport(), "will attempt to report %d device%s",
             numKnownDevices(), (numKnownDevices() > 1) ? "s" : "");

    for_each(beginDevices(), endDevices(),
             [this](std::pair<string_t, dsDev_t *> item) {
               auto dev = item.second;

               if (dev->available()) {
                 ESP_LOGI(tagReport(), "reading device %s", dev->debug().get());

                 takeBus();
                 auto rc = readDevice(dev);

                 if (rc) {
                   ESP_LOGI(tagReport(), "publishing reading for %s",
                            dev->debug().get());
                   publish(dev);
                   dev->justSeen();
                 }
                 // hold onto the bus mutex to ensure that the device publih
                 // succeds (another task doesn't change the device just read)
                 giveBus();
               } else {
                 if (dev->missing()) {
                   ESP_LOGW(tagReport(), "device missing: %s",
                            dev->debug().get());
                 }
               }
             });

    trackReport(false);
    reportMetrics();

    // case b:  wait a present duration (no temp devices)
    if (!_temp_devices_present) {
      ESP_LOGI(tagReport(), "no temperature devices, sleeping for %u ticks",
               _report_frequency);
      vTaskDelayUntil(&(_reportTask.lastWake), _report_frequency);
    }
  }
}

bool mcrDS::readDevice(dsDev_t *dev) {
  celsiusReading_t *celsius = nullptr;
  positionsReading_t *positions = nullptr;
  auto rc = false;

  if (dev->isNotValid()) {
    printInvalidDev(dev);
    return false;
  }

  // before attempting to read any device reset the bus.
  // if the reset fails then something has gone wrong with the bus
  // perform this check here so the specialized read methods can assume
  // the bus is operational and eliminate redundant code
  auto present = false;
  if ((rc = owb_reset(_ds, &present)) != OWB_STATUS_OK) {
    ESP_LOGW(tagReadDevice(), "bus reset failed [%s]", dev->debug().get());
    return rc;
  }

  switch (dev->family()) {
  case 0x10: // DS1820 (temperature sensors)
  case 0x22:
  case 0x28:
    dev->startRead();
    rc = readDS1820(dev, &celsius);
    dev->stopRead();
    if (rc)
      dev->setReading(celsius);
    break;

  case 0x29: // DS2408 (8-channel switch)
    rc = readDS2408(dev, &positions);
    if (rc)
      dev->setReading(positions);
    break;

  case 0x12: // DS2406 (2-channel switch with 1k EPROM)
    rc = readDS2406(dev, &positions);
    if (rc)
      dev->setReading(positions);
    break;

  case 0x3a: // DS2413 (Dual-Channel Addressable Switch)
    rc = readDS2413(dev, &positions);
    if (rc) {
      dev->setReading(positions);
    }
    break;

  case 0x26: // DS2438 (Smart Battery Monitor)
    rc = false;
    break;

  default:
    ESP_LOGW(tagEngine(), "unknown family 0x%02x", dev->family());
  }

  return rc;
}

// specific device scratchpad methods
bool mcrDS::readDS1820(dsDev_t *dev, celsiusReading_t **reading) {
  bool present = false;
  owb_status owb_s;
  uint8_t data[9] = {0x00};
  bool type_s = false;
  bool rc = false;

  switch (dev->family()) {
  case 0x10:
    type_s = true;
    break;
  default:
    type_s = false;
  }

  dev->startRead();
  uint8_t cmd[] = {0x55, // match rom_code
                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // rom
                   0xbe}; // read scratchpad

  dev->copyAddrToCmd(cmd);

  owb_s = owb_write_bytes(_ds, cmd, sizeof(cmd));
  owb_s = owb_read_bytes(_ds, data, sizeof(data));
  owb_reset(_ds, &present);

  if (owb_s != OWB_STATUS_OK) {
    ESP_LOGW(tagReadDS1820(), "failed to read scratchpad owb_s=%d", owb_s);
    return rc;
  }

  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    uint32_t cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00)
      raw = raw & ~7; // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20)
      raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40)
      raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }

  // calculate the crc of the received scratchpad
  uint16_t crc8 = owb_crc8_bytes(0x00, data, sizeof(data));

  if (crc8 != 0x00) {
    ESP_LOGW(tagReadDS1820(), "crc FAILED (0x%02x) for %s", crc8,
             dev->debug().get());
    return rc;
  }

  float celsius = (float)raw / 16.0;

  rc = true;
  *reading = new celsiusReading(dev->id(), lastConvertTimestamp(), celsius);

  return rc;
}

bool mcrDS::readDS2406(dsDev_t *dev, positionsReading_t **reading) {
  bool present = false;
  owb_status owb_s;
  bool rc = false;

  uint8_t cmd[] = {0x55, // match rom_code
                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // rom
                   0xaa,        // read status cmd
                   0x00, 0x00}; // address (start at beginning)

  uint8_t buff[] = {0x00, 0x00, 0x00, 0x00, 0x00, // byte 0-4: EPROM bitmaps
                    0x00, // byte 5: EPROM Factory Test Byte
                    0x00, // byte 6: Don't care (always reads 0x00)
                    0x00, // byte 7: SRAM (channel flip-flops, power, etc.)
                    0x00, 0x00}; // byte 8-9: CRC16

  dev->startRead();
  dev->copyAddrToCmd(cmd);

  owb_s = owb_write_bytes(_ds, cmd, sizeof(cmd));

  if (owb_s != OWB_STATUS_OK) {
    dev->stopRead();
    ESP_LOGW(tagReadDS2406(), "failed to send read cmd owb_s=%d", owb_s);
    return rc;
  }

  // fill buffer with bytes from DS2406, skipping the first byte
  // since the first byte is included in the CRC16
  owb_s = owb_read_bytes(_ds, buff, sizeof(buff));
  dev->stopRead();

  if (owb_s != OWB_STATUS_OK) {
    ESP_LOGW(tagReadDS2406(), "failed to read cmd results owb_s=%d", owb_s);
    return rc;
  }

  owb_s = owb_reset(_ds, &present);

  uint32_t raw = buff[sizeof(buff) - 3];

  uint32_t positions = 0x00;  // translate raw status to 0b000000xx
  if ((raw & 0x20) == 0x00) { // to represent PIO.A as bit 0
    positions = 0x01;         // and PIO.B as bit 1
  }                           // reminder to invert the bits since the device
                              // represents on/off opposite of true/false
  if ((raw & 0x40) == 0x00) {
    positions = (positions | 0x02);
  }

  rc = true;
  *reading =
      new positionsReading(dev->id(), time(nullptr), positions, (uint8_t)2);

  return rc;
}

bool mcrDS::readDS2408(dsDev_t *dev, positionsReading_t **reading) {
  bool present = false;
  owb_status owb_s;
  bool rc = false;

  // byte data[12]
  uint8_t dev_cmd[] = {
      0x55,                                           // byte 0: match ROM
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // byte 1-8: rom
      0xf5,                   // byte 9: channel access read cmd
      0x00, 0x00, 0x00, 0x00, // bytes 10-41: channel state data
      0x00, 0x00, 0x00, 0x00, // channel state data
      0x00, 0x00, 0x00, 0x00, // channel state data
      0x00, 0x00, 0x00, 0x00, // channel state data
      0x00, 0x00, 0x00, 0x00, // channel state data
      0x00, 0x00, 0x00, 0x00, // channel state data
      0x00, 0x00, 0x00, 0x00, // channel state data
      0x00, 0x00, 0x00, 0x00, // channel state data
      0x00, 0x00};            // bytes 42-43: CRC16

  dev->startRead();
  dev->copyAddrToCmd(dev_cmd);

  // send bytes through the Channel State Data device command
  owb_s = owb_write_bytes(_ds, dev_cmd, 10);

  if (owb_s != OWB_STATUS_OK) {
    dev->stopRead();
    ESP_LOGW(tagReadDS2408(), "failed to send read cmd owb_s=%d", owb_s);
    return rc;
  }

  // read 32 bytes of channel state data + 16 bits of CRC
  // into the dev_cmd
  owb_s = owb_read_bytes(_ds, (dev_cmd + 10), 34);
  dev->stopRead();

  ESP_LOGD(tagReadDS2408(), "dev_cmd after read start of buffer dump");
  ESP_LOG_BUFFER_HEX_LEVEL(tagReadDS2408(), dev_cmd, sizeof(dev_cmd),
                           ESP_LOG_DEBUG);
  ESP_LOGD(tagReadDS2408(), "dev_cmd after read end of buffer dump");

  if (owb_s != OWB_STATUS_OK) {

    ESP_LOGW(tagReadDS2408(), "failed to read cmd results owb_s=%d", owb_s);
    return rc;
  }

  owb_s = owb_reset(_ds, &present);

  // compute the crc16 over the Channel Access command through channel state
  // data (excluding the crc16 bytes)
  uint16_t crc16 =
      check_crc16((dev_cmd + 9), 33, &(dev_cmd[sizeof(dev_cmd) - 2]));

  if (!crc16) {
    ESP_LOGW(tagReadDS2408(), "crc FAILED (0x%02x) for %s", crc16,
             dev->debug().get());
    return rc;
  }

  // negate positions since device sees on/off opposite of true/false
  // uint32_t positions = (~buff[sizeof(buff) - 3]) & 0xFF; // constrain to
  // 8bits
  uint32_t positions =
      ~(dev_cmd[sizeof(dev_cmd) - 3]) & 0xFF; // constrain to 8bits

  if (reading != nullptr) {
    *reading =
        new positionsReading(dev->id(), time(nullptr), positions, (uint32_t)8);
  }
  rc = true;

  return rc;
}

bool mcrDS::readDS2413(dsDev_t *dev, positionsReading_t **reading) {
  bool present = false;
  owb_status owb_s;
  bool rc = false;

  uint8_t cmd[] = {0x55, // match rom_code
                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // rom
                   0xf5}; // pio access read

  uint8_t buff[] = {0x00, 0x00}; // byte 0-1: pio status bit assignment (x2)

  dev->startRead();
  dev->copyAddrToCmd(cmd);

  owb_s = owb_write_bytes(_ds, cmd, sizeof(cmd));

  if (owb_s != OWB_STATUS_OK) {
    dev->stopRead();
    ESP_LOGW(tagReadDS2413(), "failed to send read cmd owb_s=%d", owb_s);
    return rc;
  }

  // fill buffer with bytes from DS2406, skipping the first byte
  // since the first byte is included in the CRC16
  owb_s = owb_read_bytes(_ds, buff, sizeof(buff));
  dev->stopRead();

  if (owb_s != OWB_STATUS_OK) {
    ESP_LOGW(tagReadDS2413(), "failed to read cmd results owb_s=%d", owb_s);
    return rc;
  }

  owb_s = owb_reset(_ds, &present);

  // both bytes should be the same
  if (buff[0] != buff[1]) {
    ESP_LOGW(tagReadDS2413(), "state bytes don't match (0x%02x != 0x%02x ",
             buff[0], buff[1]);
    return rc;
  }

  uint32_t raw = buff[0];

  // NOTE: pio states are inverted at the device
  // PIO Status Bits:
  // b0: PIOA State, b1: PIOA Latch, b2: PIOB State, b3: PIOB Latch
  // b4-b7: complement of b3 to b0
  uint32_t positions = 0x00;  // translate raw status to 0b000000xx
  if ((raw & 0x01) == 0x00) { // represent PIO.A as bit 0
    positions = 0x01;
  }

  if ((raw & 0x04) == 0x00) { // represent PIO.B as bit 1
    positions = (positions | 0x02);
  }

  rc = true;
  *reading =
      new positionsReading(dev->id(), time(nullptr), positions, (uint8_t)2);

  return rc;
}

bool mcrDS::resetBus() {
  bool present = false;
  owb_status owb_s;

  owb_s = owb_reset(_ds, &present);

  if (owb_s == OWB_STATUS_OK) {
    return true;
  }

  return false;
}

void mcrDS::run(void *data) {
  owb_rmt_driver_info *rmt_driver = new owb_rmt_driver_info;
  _ds = owb_rmt_initialize(rmt_driver, _pin, RMT_CHANNEL_0, RMT_CHANNEL_1);

  _cmd_q = xQueueCreate(_max_queue_depth, sizeof(mcrCmdSwitch_t *));

  // the command task will wait for the queue which is fed by MQTTin
  xTaskCreate(&runCommand, "mcrDScmd", (4 * 1024), this, _cmdTask.priority,
              &(_cmdTask.handle));

  // the convert task will wait for the devices available bit
  xTaskCreate(&runConvert, "mcrDScon", (4 * 1024), this, _convertTask.priority,
              &(_convertTask.handle));

  // the discover task will immediate start, no reason to wait
  xTaskCreate(&runDiscover, "mcrDSdis", (4 * 1024), this,
              _discoverTask.priority, &(_discoverTask.handle));

  // the report task will wait for the temperature available bit
  // FIXME: this should be smarter and discern the difference between
  //        temperature devices and switch devices
  //        ** until this is done there will never be a report if temperature
  //        devices are not on the bus **
  xTaskCreate(&runReport, "mcrDSrep", (4 * 1024), this, _reportTask.priority,
              &(_reportTask.handle));

  owb_use_crc(_ds, true);

  ESP_LOGD(tagEngine(),
           "created ow_rmt=%p cmd_q=%p cmd_task=%p "
           "convert_task=%p discover_task=%p report_task=%p",
           _ds, (void *)_cmd_q, (void *)_cmdTask.handle,
           (void *)_convertTask.handle, (void *)_discoverTask.handle,
           (void *)_reportTask.handle);

  cmdQueue_t cmd_q = {"mcrDS", "ds", _cmd_q};
  mcrCmdQueues::registerQ(cmd_q);

  ESP_LOGI(tagEngine(), "waiting for normal ops...");
  mcr::Net::waitForNormalOps();
  ESP_LOGI(tagEngine(), "normal ops, proceeding to task loop");

  _engineTask.lastWake = xTaskGetTickCount();

  // adjust the engine task priority as we enter into the main run loop
  vTaskPrioritySet(nullptr, _engineTask.priority);

  for (;;) {
    // signal to other tasks the dsEngine task is in it's run loop
    // this ensures all other set-up activities are complete before
    engineRunning();

    // do high-level engine actions here (e.g. general housekeeping)
    vTaskDelayUntil(&(_engineTask.lastWake), _loop_frequency);
    runtimeMetricsReport();
  }
}

void mcrDS::runCommand(void *task_data) {
  mcrDS *instance = (mcrDS *)task_data;
  instance->command(instance->_handle_cmd_task_data);
  ::vTaskDelete(instance->_cmdTask.handle);
}

void mcrDS::runConvert(void *task_data) {
  mcrDS *instance = (mcrDS *)task_data;
  instance->convert(instance->_convertTask.data);
  ::vTaskDelete(instance->_convertTask.handle);
}

void mcrDS::runDiscover(void *task_data) {
  mcrDS *instance = (mcrDS *)task_data;
  instance->discover(instance->_discoverTask.data);
  ::vTaskDelete(instance->_discoverTask.handle);
}

void mcrDS::runReport(void *task_data) {
  mcrDS *instance = (mcrDS *)task_data;
  instance->report(instance->_reportTask.data);
  ::vTaskDelete(instance->_reportTask.handle);
}

bool mcrDS::setDS2406(mcrCmdSwitch_t &cmd, dsDev_t *dev) {
  bool present = false;
  owb_status owb_s;
  bool rc = false;

  positionsReading_t *reading = (positionsReading_t *)dev->reading();
  uint32_t mask = cmd.mask().to_ulong();
  uint32_t tobe_state = cmd.state().to_ulong();
  uint32_t asis_state = reading->state();

  bool pio_a = (mask & 0x01) ? (tobe_state & 0x01) : (asis_state & 0x01);
  bool pio_b = (mask & 0x02) ? (tobe_state & 0x02) : (asis_state & 0x02);

  uint32_t new_state = (!pio_a << 5) | (!pio_b << 6) | 0xf;

  uint8_t dev_cmd[] = {
      0x55,                                           // byte 0: match rom_code
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // byte 1-8: rom
      0x55,                                           // byte 9: write status
      0x07, 0x00, // byte:10-11: address of status byte
      0x00,       // byte 12: status byte to send (populated below)
      0x00, 0x00  // byte 13-14: crc16
  };

  size_t dev_cmd_size = sizeof(dev_cmd);
  int crc16_idx = dev_cmd_size - 2;
  dev_cmd[12] = new_state;

  dev->startWrite();
  dev->copyAddrToCmd(dev_cmd);

  // send the device the command excluding the crc16 bytes
  owb_s = owb_write_bytes(_ds, dev_cmd, dev_cmd_size - 2);

  if (owb_s != OWB_STATUS_OK) {
    dev->stopWrite();
    ESP_LOGW(tagSetDS2406(), "failed to send read cmd owb_s=%d", owb_s);
    return rc;
  }

  // device sends back the crc16 of the transmittd data (all bytes)
  // so, read just the two crc16 bytes into the dev_cmd
  owb_s = owb_read_bytes(_ds, (dev_cmd + crc16_idx), 2);

  if (owb_s != OWB_STATUS_OK) {
    dev->stopWrite();
    ESP_LOGW(tagSetDS2406(), "failed to read cmd results owb_s=%d", owb_s);
    return rc;
  }

  dev->stopWrite();

  bool crc_good = check_crc16((dev_cmd + 9), 4, &dev_cmd[crc16_idx]);

  if (crc_good) {
    owb_s = owb_write_byte(_ds,
                           0xff); // writing 0xFF will copy scratchpad to status

    if (owb_s != OWB_STATUS_OK) {
      ESP_LOGW(tagSetDS2406(), "failed to copy scratchpad to status owb_s=%d",
               owb_s);
      return rc;
    }

    owb_s = owb_reset(_ds, &present);
    rc = true;
  } else {
    ESP_LOGW(tagSetDS2406(), "crc16 failure");
  }

  return rc;
}

bool mcrDS::setDS2408(mcrCmdSwitch_t &cmd, dsDev_t *dev) {
  bool present = false;
  owb_status owb_s;
  bool rc = false;

  // read the device to ensure we have the current state
  // important because setting the new state relies, in part, on the existing
  // state for the pios not changing
  if (readDevice(dev) == false) {
    ESP_LOGW(tagSetDS2408(), "read before set failed for %s",
             dev->debug().get());
    return rc;
  }

  positionsReading_t *reading = (positionsReading_t *)dev->reading();

  uint32_t mask = cmd.mask().to_ulong();
  uint32_t changes = cmd.state().to_ulong();
  uint32_t asis_state = reading->state();
  uint32_t new_state = 0x00;
  // uint32_t report_state = 0x00;

  // use XOR tricks to apply the state changes to the as_is state using the
  // mask computed
  new_state = asis_state ^ ((asis_state ^ changes) & mask);

  // now negate the new_state since the device actually represents
  // on = 0
  // off = 1

  // report_state = new_state;
  new_state = (~new_state) & 0xFF; // constrain to 8-bits

  dev->startWrite();
  owb_s = owb_reset(_ds, &present);

  uint8_t dev_cmd[] = {0x55, // byte 0: match rom_code
                       0x00, // byte 1-8: rom
                       0x00,
                       0x00,
                       0x00,
                       0x00,
                       0x00,
                       0x00,
                       0x00,
                       0x5a,                 // byte 9: actual device cmd
                       (uint8_t)new_state,   // byte10 : new state
                       (uint8_t)~new_state}; // byte11: inverted state

  dev->startWrite();
  dev->copyAddrToCmd(dev_cmd);
  owb_s = owb_write_bytes(_ds, dev_cmd, sizeof(dev_cmd));

  if (owb_s != OWB_STATUS_OK) {
    ESP_LOGW(tagSetDS2408(), "device cmd failed for %s owb_s=%d",
             dev->debug().get(), owb_s);
    dev->stopWrite();
    return rc;
  }

  uint8_t check[2] = {0x00};
  owb_s = owb_read_bytes(_ds, check, sizeof(check));

  if (owb_s != OWB_STATUS_OK) {
    ESP_LOGW(tagSetDS2408(), "read of check bytes failed for %s owb_s=%d",
             dev->debug().get(), owb_s);
    dev->stopWrite();
    return rc;
  }

  owb_s = owb_reset(_ds, &present);
  dev->stopWrite();

  // check what the device returned to determine success or failure
  // byte 0 = 0xAA is a success, byte 1 = new_state
  // this might be a bit of a hack however let's accept success if either
  // the check byte is 0xAA, 0x2A *OR* the reported dev_state == new_state
  // this handles the occasional situation where there is a single dropped
  // bit in either (but hopefully not both)
  uint32_t dev_state = check[1];
  if (((check[0] | 0x80) == 0xaa) || (dev_state == new_state)) {
    // cmd_bitset_t b0 = check[0];
    // cmd_bitset_t b1 = check[1];
    ESP_LOGI(tagSetDS2408(), "CONFIRMED for %s", dev->debug().get());
    ESP_LOGI(tagSetDS2408(),
             "CONFIRMED expected 0x%02x==0xaa *OR* 0x%02x==0x%02x", check[0],
             new_state, dev_state);

    rc = true;
  } else {
    ESP_LOGW(tagSetDS2408(), "FAILED for %s", dev->debug().get());
    ESP_LOGW(tagSetDS2408(), "FAILED expected 0x%02x==aa *OR* 0x%02x==0x%02x",
             check[0], new_state, dev_state);
  }

  return rc;
}

bool mcrDS::setDS2413(mcrCmdSwitch_t &cmd, dsDev_t *dev) {
  bool present = false;
  owb_status owb_s;
  bool rc = false;

  // read the device to ensure we have the current state
  // important because setting the new state relies, in part, on the existing
  // state for the pios not changing
  if (readDevice(dev) == false) {
    ESP_LOGW(tagSetDS2413(), "read before set failed for %s",
             dev->debug().get());
    return rc;
  }

  positionsReading_t *reading = (positionsReading_t *)dev->reading();

  uint32_t mask = cmd.mask().to_ulong();
  uint32_t changes = cmd.state().to_ulong();
  uint32_t asis_state = reading->state();
  uint32_t new_state = 0x00;

  // use XOR tricks to apply the state changes to the as_is state using the
  // mask computed
  new_state = asis_state ^ ((asis_state ^ changes) & mask);

  // report_state = new_state;
  new_state = (~new_state) & 0xFF; // constrain to 8-bits

  uint8_t dev_cmd[] = {0x55, // byte 0: match rom_code
                       0x00, // byte 1-8: rom
                       0x00,
                       0x00,
                       0x00,
                       0x00,
                       0x00,
                       0x00,
                       0x00,
                       0x5a,                 // byte 9: PIO Access Write cmd
                       (uint8_t)new_state,   // byte10 : new state
                       (uint8_t)~new_state}; // byte11: inverted state

  dev->startWrite();
  dev->copyAddrToCmd(dev_cmd);
  owb_s = owb_write_bytes(_ds, dev_cmd, sizeof(dev_cmd));

  if (owb_s != OWB_STATUS_OK) {
    ESP_LOGW(tagSetDS2413(), "device cmd failed for %s owb_s=%d",
             dev->debug().get(), owb_s);
    dev->stopWrite();
    return rc;
  }

  uint8_t check[2] = {0x00};
  owb_s = owb_read_bytes(_ds, check, sizeof(check));

  if (owb_s != OWB_STATUS_OK) {
    ESP_LOGW(tagSetDS2413(), "read of check bytes failed for %s owb_s=%d",
             dev->debug().get(), owb_s);
    dev->stopWrite();
    return rc;
  }

  owb_s = owb_reset(_ds, &present);
  dev->stopWrite();

  // check what the device returned to determine success or failure
  // byte 0 = 0xAA is a success, byte 1 = new_state
  // this might be a bit of a hack however let's accept success if either
  // the check byte is 0xAA *OR* the reported dev_state == new_state
  // this handles the occasional situation where there is a single dropped
  // bit in either (but hopefully not both)
  uint32_t dev_state = check[1];
  if ((check[0] == 0xaa) || (dev_state == (new_state & 0xff))) {
    cmd_bitset_t b0 = check[0];
    cmd_bitset_t b1 = check[1];
    ESP_LOGD(tagSetDS2413(), "CONFIRMED check[0]=0b%s check[1]=0b%s for %s",
             b0.to_string().c_str(), b1.to_string().c_str(),
             dev->debug().get());
    rc = true;
  } else {
    ESP_LOGW(tagSetDS2413(), "FAILED check[0]=0x%x check[1]=0x%x for %s",
             check[0], check[1], dev->debug().get());
  }

  return rc;
}

void mcrDS::suspend() {
  struct tasks {
    const char *name;
    mcrTask_t *task;
  };

  struct tasks suspend[] = {{.name = tagConvert(), .task = &_convertTask},
                            {.name = tagDiscover(), .task = &_discoverTask},
                            {.name = tagReport(), .task = &_reportTask},
                            {.name = tagCommand(), .task = &_cmdTask}};

  uint8_t num_suspend = sizeof(suspend) / sizeof(tasks);
  for (uint8_t i = 0; i < num_suspend; i++) {
    ESP_LOGW(tagEngine(), "suspending %s(%p)", suspend[i].name,
             suspend[i].task->handle);
    ::vTaskSuspend(suspend[i].task->handle);
  }

  this->mcrEngine::suspend();
}

bool mcrDS::check_crc16(const uint8_t *input, uint16_t len,
                        const uint8_t *inverted_crc, uint16_t crc) {
  crc = ~crc16(input, len, crc);
  return (crc & 0xFF) == inverted_crc[0] && (crc >> 8) == inverted_crc[1];
}

uint16_t mcrDS::crc16(const uint8_t *input, uint16_t len, uint16_t crc) {
  static const uint8_t oddparity[16] = {0, 1, 1, 0, 1, 0, 0, 1,
                                        1, 0, 0, 1, 0, 1, 1, 0};

  for (uint16_t i = 0; i < len; i++) {
    // Even though we're just copying a byte from the input,
    // we'll be doing 16-bit computation with it.
    uint16_t cdata = input[i];
    cdata = (cdata ^ crc) & 0xff;
    crc >>= 8;

    if (oddparity[cdata & 0x0F] ^ oddparity[cdata >> 4])
      crc ^= 0xC001;

    cdata <<= 6;
    crc ^= cdata;
    cdata <<= 1;
    crc ^= cdata;
  }

  return crc;
}

void mcrDS::printInvalidDev(dsDev_t *dev) {
  if (dev == nullptr) {
    ESP_LOGW(tagEngine(), "%s dev == nullptr", __PRETTY_FUNCTION__);
    return;
  }

  ESP_LOGW(tagEngine(), "%s dev id=%s", __PRETTY_FUNCTION__,
           (const char *)dev->id().c_str());
}
} // namespace mcr
