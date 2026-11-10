// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace nxrt {
// Bootstrap only: memory/process SVCs and adoption of the calling host thread.
// Creating/scheduling guest worker threads and IPC services are not implemented.
void android_configure_fefw();
void svc_memory_reset_for_tests();
void svc_info_reset_for_tests();
}
