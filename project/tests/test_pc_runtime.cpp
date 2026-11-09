// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_framework.h"
#include "../src/fefw_pc_policy.h"
#include "nxrt/frame_limiter.h"
#include <cmath>
NXTEST_GROUP(fefw_pc)
NXTEST(pacing_respects_game_and_host_caps) {
    nxrt::FrameLimiter l;
    l.set_target_fps(144); l.set_game_fps(60); CHECK(l.effective_fps() == 60);
    l.set_game_fps(120); CHECK(l.effective_fps() == 120);
    l.set_target_fps(75); CHECK(l.effective_fps() == 75);
    l.set_target_fps(0); CHECK(l.effective_fps() == 120);
    l.set_game_fps(0); CHECK(l.effective_fps() == 0);
}
NXTEST(smooth_clock_converges_when_gpu_misses_120) {
    float debt = 0; double stepped = 0, elapsed = 0;
    for (int i = 0; i < 12000; ++i) {
        float e = i % 2 ? 60.0f / 110 : 60.0f / 117;
        elapsed += e; stepped += fefw::pc::smooth_step(e, 0.5f, 120, debt);
    }
    CHECK(std::abs(stepped / elapsed - 1) < 0.001);
    CHECK(fefw::pc::smooth_step(30, 30, 120, debt) == 30 && debt == 0);
}
NXTEST(render_sizes_keep_ssao_blocks_and_target_budget) {
    int w = 1920, h = 1080; fefw::pc::safe_render_size(1280, 720, w, h);
    CHECK(w == 1280 && h == 720);
    w = 2560; h = 1440; fefw::pc::safe_render_size(3840, 2160, w, h);
    CHECK(w == 1920 && h == 1080);
    w = 1707; h = 961; fefw::pc::safe_render_size(2560, 1440, w, h);
    CHECK(w == 1706 && h == 960);
}
