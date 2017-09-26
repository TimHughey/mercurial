/*
    reading.h - Readings used within Master Control Remote
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

#ifndef reading_h
#define reading_h

#ifdef __cplusplus
#if ARDUINO >= 100
#include <Arduino.h>
#else
#include <WProgram.h>
#endif

#include <ArduinoJson.h>
#include <TimeLib.h>
#include <WiFi101.h>
#include <elapsedMillis.h>

#include "../devs/dev_id.hpp"

typedef class Reading Reading_t;
class Reading {
private:
  // defines which values are populated
  typedef enum { UNDEF, TEMP, RH, SWITCH, SOIL, PH } reading_t;

  // id and metadata for the reading
  mcrDevID_t _id;
  time_t _mtime = now();
  reading_t _type = UNDEF;

  // actual reading data
  float _celsius;
  float _relhum;
  float _soil;
  float _ph;
  uint8_t _state;
  uint8_t _bits;
  bool _cmd_ack = false;
  time_t _latency = 0;
  char _cid[39] = {0x00};

  void jsonCommon(JsonObject &root);

  virtual void populateJSON(JsonObject &root){};
  const char *typeAsString();

public:
  // default constructor, Reading type undefined
  Reading() {}
  virtual ~Reading(){};

  Reading(mcrDevID_t &id) {
    _id = id;
    _mtime = now();
    _type = UNDEF;
  }

  Reading(mcrDevID_t &id, time_t mtime) {
    _id = id;
    _mtime = mtime;
    _type = UNDEF;
  }

  // temperature only
  // Reading(char *id, float celsius) {
  //  strcpy(_id, id);
  //  _mtime = now();
  //  _type = TEMP;
  //  _celsius = celsius;
  //  }
  Reading(mcrDevID_t &id, time_t mtime, float celsius) {
    _id = id;
    _mtime = mtime;
    _type = TEMP;
    _celsius = celsius;
  }

  // relative humidity
  // Reading(const char *id, float celsius, float relhum) {
  //  strcpy(_id, id);
  //  _mtime = now();
  //  _type = RH;
  //  _celsius = celsius;
  //  _relhum = relhum;
  //  }

  Reading(mcrDevID_t &id, time_t mtime, float celsius, float relhum) {
    _id = id;
    _mtime = mtime;
    _type = RH;
    _celsius = celsius;
    _relhum = relhum;
  }

  // switch states
  Reading(mcrDevID_t &id, time_t mtime, uint8_t state, uint8_t bits) {
    _id = id;
    _mtime = mtime;
    _type = SWITCH;
    _state = state;
    _bits = bits;
  }

  void setCmdAck(time_t latency, const char *cid = NULL) {
    _cmd_ack = true;
    _latency = latency;

    if (cid) {
      _cid[0] = 0x00;
      strncat(_cid, cid, 38);
    }
  }

  uint8_t state() { return _state; };
  char *json();
};

#endif // __cplusplus
#endif // reading_h