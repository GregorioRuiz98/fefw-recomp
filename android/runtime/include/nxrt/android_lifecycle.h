// SPDX-License-Identifier: GPL-3.0-or-later
// Host lifecycle bridge; no generated-code ABI is needed by the activity/JNI.
#pragma once
namespace nxrt::services::amctl {
void set_docked(bool docked);
void set_focus(bool in_focus);
bool docked();
bool in_focus();
}
