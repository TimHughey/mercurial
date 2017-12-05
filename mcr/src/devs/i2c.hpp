/*
    mcr_i2c.h - Master Control Remote I2C
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

#ifndef i2c_dev_h
#define i2c_dev_h

#ifdef __cplusplus

#if ARDUINO >= 100
#include <Arduino.h>
#else
#include <WProgram.h>
#endif

#include "../include/readings.hpp"
#include "../misc/util.hpp"
#include "base.hpp"

typedef class i2cDev i2cDev_t;

class i2cDev : public mcrDev {
public:
  i2cDev() {}
  static const char *i2cDevDesc(uint8_t addr);

private:
  static const uint8_t _i2c_max_addr_len = 1;
  static const uint8_t _i2c_max_id_len = 30;
  static const uint8_t _i2c_addr_byte = 0;
  bool _use_multiplexer = false; // is the multiplexer is needed to reach device
  uint8_t _bus = 0; // if using multiplexer then this is the bus number
                    // where the device is hoste
public:
  // construct a new i2cDev with a known address and compute the id
  i2cDev(mcrDevAddr_t &addr, bool use_multiplexer = false, uint8_t bus = 0);

  uint8_t devAddr();
  boolean useMultiplexer();
  uint8_t bus();

  // info / debugg functions
  void printReadMS(const char *func, uint8_t indent = 2);
};

#endif // __cplusplus
#endif // i2c_dev_h