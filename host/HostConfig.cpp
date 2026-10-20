// HostConfig: the one configuration in use (see HostConfig.h).
#include "host/HostConfig.h"

namespace rapture {

namespace {
const HostConfig g_default;                 // all empty: used by tests that do not install a configuration
const HostConfig* g_config = &g_default;
} // namespace

const HostConfig& Config() { return *g_config; }
void SetConfig(const HostConfig* cfg) { g_config = cfg ? cfg : &g_default; }

} // namespace rapture
