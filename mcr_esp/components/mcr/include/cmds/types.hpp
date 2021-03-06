/*
    types.hpp - Master Control Command Base Class
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

#ifndef mcr_cmd_types_h
#define mcr_cmd_types_h

#include <bitset>
#include <cstdlib>
#include <map>
#include <string>

#include "devs/base.hpp"

namespace mcr {

typedef enum class mcrCmdType {
  unknown,
  none,
  timesync,
  setswitch,
  heartbeat,
  setname,
  restart,
  enginesSuspend,
  otaHTTPS,
  pwm
} mcrCmdType_t;

typedef class mcrCmdTypeMap mcrCmdTypeMap_t;
class mcrCmdTypeMap {
public:
  static mcrCmdTypeMap_t *instance();
  static mcrCmdType_t fromByte(char byte) {
    return instance()->decodeByte(byte);
  }
  static mcrCmdType_t fromString(const string_t &cmd) {
    return instance()->find(cmd);
  }

private:
  mcrCmdTypeMap();

  mcrCmdType_t decodeByte(char byte);
  mcrCmdType_t find(const string_t &cmd);
};

} // namespace mcr
#endif
