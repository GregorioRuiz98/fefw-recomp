// SPDX-License-Identifier: GPL-3.0-or-later
// The gauge registry from nxrt/diag/gauge.cpp; the Windows soak sampler is not linked.
#include "nxrt/gauge.h"
#include <map>
#include <mutex>

namespace nxrt {
namespace {
struct Registry {
    std::mutex lock;
    std::map<std::string, std::function<int64_t()>> values;
};
Registry& registry() {
    // Same lifetime contract as upstream: global subsystem destructors unregister.
    static Registry* value = new Registry;
    return *value;
}
}
void gauge_register(const char* name, std::function<int64_t()> fn) {
    auto& state = registry();
    std::lock_guard lock(state.lock);
    state.values[name] = std::move(fn);
}
void gauge_unregister(const char* name) {
    auto& state = registry();
    std::lock_guard lock(state.lock);
    state.values.erase(name);
}
std::vector<std::pair<std::string, int64_t>> gauge_read_all() {
    auto& state = registry();
    std::lock_guard lock(state.lock);
    std::vector<std::pair<std::string, int64_t>> result;
    for (const auto& [name, fn] : state.values) result.emplace_back(name, fn());
    return result;
}
GaugeCounter::GaugeCounter(const char* name) : name_(name) {
    gauge_register(name, [this] { return get(); });
}
GaugeCounter::~GaugeCounter() { gauge_unregister(name_.c_str()); }
}  // namespace nxrt
