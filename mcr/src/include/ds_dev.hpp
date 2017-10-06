/*
    ds_dev.h - Master Control Dalla Semiconductor Device
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

#ifndef ds_dev_h
#define ds_dev_h

#ifdef __cplusplus

#if ARDUINO >= 100
#include <Arduino.h>
#else
#include <WProgram.h>
#endif

#include "../include/mcr_dev.hpp"
#include "../include/ref_id.hpp"

typedef class dsDev dsDev_t;

class dsDev : public mcrDev {
private:
  // static const uint8_t _addr_len = 8;
  // static const uint8_t _id_len = 18;
  static const uint8_t _ds_max_addr_len = 8;
  static const uint8_t _family_byte = 0;
  static const uint8_t _crc_byte = 7;

  static const uint8_t _family_DS2408 = 0x29;
  static const uint8_t _family_DS2406 = 0x12;

  bool _power = false; // is the device powered?

  static const char *familyDesc(uint8_t family);
  const char *familyDesc();

public:
  dsDev();
  dsDev(mcrDevAddr_t &addr, bool power = false);

  uint8_t family();
  uint8_t crc();
  bool isPowered();
  void setReadingCmdAck(time_t latency, mcrRefID_t &refid);
  Reading_t *reading();

  bool isDS2406();
  bool isDS2408();

  static uint8_t *parseId(char *name);

  // info / debugg functions
  void printReadMS(const char *func, uint8_t indent = 2);
  void printWriteMS(const char *func, uint8_t indent = 2);
  void printPresenceFailed(const char *func, uint8_t indent = 2);

  // static member function for validating an address (ROM) is validAddress
  static bool validAddress(uint8_t *addr);
  bool validAddress(mcrDevAddr_t *addr);

  void debug(bool newline = false);
};

typedef class dsDev dsDev_t;

#endif // __cplusplus
#endif // ds_dev_h