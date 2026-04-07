#pragma once

#include <cstdint>

#include "Rudp/Config.hpp"
#include "Rudp/RuntimeLogger.hpp"

namespace Rudp::Runtime {

void run_server_app(const Rudp::Config::RuntimeProfile& profile,
                    const LogSink& logger);

}  // namespace Rudp::Runtime
