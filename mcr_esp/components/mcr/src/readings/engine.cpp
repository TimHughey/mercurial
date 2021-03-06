/*
    celsius.cpp - Master Control Remote Celsius Reading
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

#include <cstdlib>
#include <ctime>

#include "readings/engine.hpp"

namespace mcr {
EngineReading::EngineReading(const std::string &engine, uint64_t discover_us,
                             uint64_t convert_us, uint64_t report_us,
                             uint64_t switch_cmd_us)
    : Reading(), engine_(engine), discover_us_(discover_us),
      convert_us_(convert_us), report_us_(report_us),
      switch_cmd_us_(switch_cmd_us) {
  _type = ReadingType_t::ENGINE;
};

bool EngineReading::hasNonZeroValues() {
  return (discover_us_ > 0) || (convert_us_ > 0) || (report_us_ > 0);
}

void EngineReading::populateJSON(JsonDocument &doc) {
  doc["metric"] = "engine_phase";
  doc["engine"] = engine_;
  doc["discover_us"] = discover_us_;
  doc["convert_us"] = convert_us_;
  doc["report_us"] = report_us_;
  doc["switch_cmd_us"] = switch_cmd_us_;
};
} // namespace mcr
