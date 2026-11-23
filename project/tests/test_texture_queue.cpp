// SPDX-License-Identifier: GPL-3.0-or-later
// Compile the real queue implementation into this CPU-only test so internal
// scheduling states can be exercised without racing background GPU workers.
#include "test_framework.h"
#include FEFW_TEXPREP_SOURCE

namespace nxrt::nvn::be::texprep {
NXTEST_GROUP(fefw_texture_queue)
namespace {
struct QueueFixture {
    State state;
    State* previous = g_state;
    QueueFixture() { g_state = &state; }
    ~QueueFixture() { g_state = previous; }
};
void entry(State& s, Texture& t, Entry::State status, uint64_t bytes, uint64_t seq) {
    auto& e = s.map[&t];
    e.st = status; e.held = bytes; e.seq = seq;
    e.keep = Ref<Texture>::share(&t);
    s.held_bytes += bytes;
    s.seq = std::max(s.seq, seq);
}
}
NXTEST(requeued_result_is_not_evicted_while_its_entry_is_in_use) {
    Texture rewritten, finished, running;
    QueueFixture f;
    auto& s = f.state;
    s.budget = 24;
    rewritten.storage_size = 12;
    entry(s, rewritten, Entry::Done, 1, 1);
    entry(s, finished, Entry::Done, 12, 2);
    entry(s, running, Entry::Running, 8, 3);
    std::lock_guard lk(s.mu);
    schedule_locked(&rewritten);
    const auto it = s.map.find(&rewritten);
    REQUIRE(it != s.map.end());
    CHECK(it->second.st == Entry::Queued);
    CHECK(it->second.held == 12);
    CHECK(s.map.count(&finished) == 0);
    CHECK(s.map.count(&running) == 1);
    CHECK(s.held_bytes == 20);
    CHECK(s.st.evicted == 1);
    REQUIRE(s.q.size() == 1);
    CHECK(s.q.front().seq == it->second.seq);
}
NXTEST(requeue_without_pressure_keeps_one_job_and_balanced_bytes) {
    Texture rewritten;
    QueueFixture f;
    auto& s = f.state;
    s.budget = 24; rewritten.storage_size = 12;
    entry(s, rewritten, Entry::Done, 8, 1);
    std::lock_guard lk(s.mu);
    schedule_locked(&rewritten);
    schedule_locked(&rewritten);
    CHECK(s.map.at(&rewritten).st == Entry::Queued);
    CHECK(s.held_bytes == 12);
    CHECK(s.q.size() == 1);
}
NXTEST(requeue_that_cannot_fit_preserves_running_work_and_releases_its_entry) {
    Texture rewritten, running;
    QueueFixture f;
    auto& s = f.state;
    s.budget = 24; rewritten.storage_size = 12;
    entry(s, rewritten, Entry::Done, 1, 1);
    entry(s, running, Entry::Running, 20, 2);
    std::lock_guard lk(s.mu);
    schedule_locked(&rewritten);
    CHECK(s.map.count(&rewritten) == 0);
    CHECK(s.map.at(&running).st == Entry::Running);
    CHECK(s.held_bytes == 20);
    CHECK(s.q.empty());
    CHECK(s.st.skipped_budget == 1);
}
}
