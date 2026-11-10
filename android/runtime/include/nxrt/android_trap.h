// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <stdexcept>

namespace nxrt {
// An engine error, not a guest C++ exception. Catch at the Android worker/JNI boundary.
class AndroidGuestTrap final : public std::runtime_error {
public:
    AndroidGuestTrap(const char* reason, uint64_t address)
        : std::runtime_error(reason), address_(address) {}
    uint64_t address() const noexcept { return address_; }
private:
    uint64_t address_;
};
}  // namespace nxrt
