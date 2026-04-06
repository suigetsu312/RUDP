#include "Rudp/Protocol.hpp"

namespace Rudp {

bool Header::hasFlag(Flag flag) const noexcept {
  return (flags & static_cast<Flags>(flag)) != 0;
}

}  // namespace Rudp
