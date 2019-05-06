
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_spi_flash.h>

#include "cmds/cmd_ota.hpp"
#include "engines/ds_engine.hpp"
#include "engines/i2c_engine.hpp"
#include "misc/mcr_restart.hpp"
#include "net/mcr_net.hpp"
#include "protocols/mqtt.hpp"

static const char *TAG = "mcrCmdOTA";
static const char *k_host = "host";
static const char *k_reboot_delay_ms = "reboot_delay_ms";
static const char *k_fw_url = "fw_url";

static bool _ota_in_progress = false;
static float _ota_elapsed_sec = 0.0;

static char _shared_msg[128];
// prevent dup strings
static const char *_shared_msg_fmt = "[%s] OTA elapsed(%0.2fs)";

extern const uint8_t ca_start[] asm("_binary_ca_pem_start");
extern const uint8_t ca_end[] asm("_binary_ca_pem_end");

mcrCmdOTA::mcrCmdOTA(mcrCmdType_t type, JsonDocument &doc) : mcrCmd(type, doc) {
  if (doc.isNull() == false) {
    _host = doc[k_host] | "no_host";
    _fw_url = doc[k_fw_url] | "none";
    _reboot_delay_ms = doc[k_reboot_delay_ms] | 0;
  }
}

void mcrCmdOTA::doUpdate() {
  const esp_partition_t *run_part = esp_ota_get_running_partition();
  esp_http_client_config_t config = {};
  config.url = _fw_url.c_str();
  config.cert_pem = (char *)ca_start;
  config.event_handler = mcrCmdOTA::httpEventHandler;
  config.timeout_ms = 1000;

  if (_ota_in_progress) {
    ESP_LOGI(TAG, "ota in-progress, ignoring spurious begin");
    return;
  }

  _ota_in_progress = true;
  mcrI2c::instance()->suspend();
  mcrDS::instance()->suspend();
  mcr::Net::suspendNormalOps();

  mcrMQTT::otaPrep();

  uint64_t ota_start_us = esp_timer_get_time();

  ESP_LOGI(TAG, "part(run) name(%-8s) addr(0x%x)", run_part->label,
           run_part->address);

  esp_err_t esp_rc = esp_https_ota(&config);
  _ota_elapsed_sec = (float)((esp_timer_get_time() - ota_start_us) / 1000000.0);

  if (esp_rc == ESP_OK) {
    snprintf(_shared_msg, sizeof(_shared_msg), _shared_msg_fmt,
             esp_err_to_name(esp_rc), _ota_elapsed_sec);

    ESP_LOGI(TAG, "%s", _shared_msg);

    mcrRestart::instance()->restart(_shared_msg, __PRETTY_FUNCTION__,
                                    reboot_delay_ms());
  } else {
    snprintf(_shared_msg, sizeof(_shared_msg), _shared_msg_fmt,
             esp_err_to_name(esp_rc), _ota_elapsed_sec);
    ESP_LOGE(TAG, "%s", _shared_msg);

    mcrRestart::instance()->restart(_shared_msg, __PRETTY_FUNCTION__);
  }
}

bool mcrCmdOTA::process() {
  using mcr::Net;

  bool this_host = (_host.compare(Net::hostID()) == 0) ? true : false;

  if (this_host == false) {
    ESP_LOGI(TAG, "OTA command not for us, ignoring.");
    return true;
  }

  // 1. if _raw is nullptr then this is a cmd (not a data block)
  // 2. check this command is addressed to this host
  switch (type()) {

  case mcrCmdType::otaHTTPS:
    ESP_LOGI(TAG, "OTA via HTTPS requested");
    doUpdate();
    break;

  case mcrCmdType::restart:
    mcrRestart::instance()->restart("restart requested", __PRETTY_FUNCTION__,
                                    reboot_delay_ms());
    break;

  default:
    ESP_LOGW(TAG, "unknown ota command, ignoring");
    break;
  };

  return true;
}

const unique_ptr<char[]> mcrCmdOTA::debug() {
  unique_ptr<char[]> debug_str(new char[strlen(TAG) + 1]);

  strcpy(debug_str.get(), TAG);

  return move(debug_str);
}

//
// STATIC!
//
esp_err_t mcrCmdOTA::httpEventHandler(esp_http_client_event_t *evt) {
  switch (evt->event_id) {
  case HTTP_EVENT_ERROR:
    // ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    // ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
    break;
  case HTTP_EVENT_HEADER_SENT:
    // ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
    break;
  case HTTP_EVENT_ON_HEADER:
    ESP_LOGI(TAG, "OTA HTTPS HEADER: key(%s), value(%s)", evt->header_key,
             evt->header_value);
    break;
  case HTTP_EVENT_ON_DATA:
    // ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
    break;
  case HTTP_EVENT_ON_FINISH:
    // ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
    break;
  case HTTP_EVENT_DISCONNECTED:
    // ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
    break;
  }
  return ESP_OK;
}
