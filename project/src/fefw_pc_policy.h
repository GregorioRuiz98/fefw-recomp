// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
namespace fefw::pc {
// Goldoire's measured-time correction. Stalls retain the title's original rule.
inline float smooth_step(float elapsed, float chosen, int target, float& debt) {
    const float period = 60.0f / std::max(1, target);
    if (!(elapsed > 0) || !std::isfinite(elapsed) || !std::isfinite(chosen) || elapsed > 8 * period) { debt = 0; return chosen; }
    const float used = std::max(0.25f * period, chosen + 0.25f * (debt + elapsed - chosen));
    debt = std::clamp(debt + elapsed - used, -4 * period, 4 * period);
    return used;
}
inline void safe_render_size(int ow, int oh, int& rw, int& rh) {
    rw = std::clamp(rw & ~1, 64, ow & ~1); rh = std::clamp(rh & ~1, 64, oh & ~1);
    // Keep the supported title's existing render-target pool within its budget.
    if (static_cast<long long>(ow) * oh > 2560 * 1440) {
        rw = std::min(rw, (ow / 2) & ~1); rh = std::min(rh, (oh / 2) & ~1);
    }
}
}
