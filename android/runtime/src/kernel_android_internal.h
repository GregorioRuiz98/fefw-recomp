// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nxrt/kernel.h"
#include <mutex>
namespace nxrt::kernel::detail {
extern std::mutex kernel_lock;
void thread_mark_exited(KThread& thread);
void destroy_waiter(KThread& thread);
void sync_reset();
void notify_failure();
}
