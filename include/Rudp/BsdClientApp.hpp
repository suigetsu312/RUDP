#pragma once

#include <cstdint>
#include <string_view>

#include "Rudp/RuntimeLogger.hpp"

namespace Rudp::Runtime {

void run_client_app(std::string_view server_address,
                    std::uint16_t port,
                    const LogSink& logger);

}  // namespace Rudp::Runtime
