// SPDX-License-Identifier: GPL-3.0-or-later
// JNI exercises the same nxrt Android memory target that future game code links.
#include <jni.h>
#include "test_runner.h"

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_gregorioruiz98_fefw_NativeBridge_runtimeMemoryTest(JNIEnv* environment, jclass) {
    try { return environment->NewStringUTF(nxtest::run_memory_tests().c_str()); }
    catch (...) { return environment->NewStringUTF("{\"status\":\"FAIL\",\"failures\":[\"Native runtime test exception\"],\"game_runtime_available\":false}"); }
}
