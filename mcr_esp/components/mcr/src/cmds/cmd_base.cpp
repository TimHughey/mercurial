
#include "cmds/cmd_base.hpp"

// static const char *TAG = "mcrCmd";
static const char *k_mtime = "mtime";
static const char *k_cmd = "cmd";

mcrCmd::mcrCmd(JsonDocument &doc) { populate(doc); }

mcrCmd::mcrCmd(mcrCmdType_t type) {
  _mtime = time(nullptr);
  _type = type;
}

mcrCmd::mcrCmd(mcrCmdType_t type, JsonDocument &doc) : _type(type) {
  populate(doc);
}

time_t mcrCmd::latency() {
  int64_t latency = esp_timer_get_time() - _latency;
  return latency;
}

void mcrCmd::populate(JsonDocument &doc) {
  if (doc.isNull()) {
    // there should be some warning here OR determine this check isn't
    // required and remove it!
  } else {
    _mtime = doc[k_mtime] | time(nullptr);
    _type = mcrCmdTypeMap::fromString(doc[k_cmd] | "unknown");
  }
}

const std::string mcrCmd::debug() {
  std::ostringstream debug_str;
  float latency_ms = (float)latency() / 1000.0;
  float parse_ms = (float)_parse_us / 1000.0;
  float create_ms = (float)_create_us / 1000.0;

  debug_str << "mcrCmd(latency=" << latency_ms << "ms parse=" << parse_ms
            << "ms create=" << create_ms << "ms)";
  return debug_str.str();
}
