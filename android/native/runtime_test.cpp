// SPDX-License-Identifier: GPL-3.0-or-later
// JNI exercises the same nxrt Android memory target that future game code links.
#include <jni.h>
#include "test_runner.h"
#include "nxrt/android_session.h"

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_gregorioruiz98_fefw_NativeBridge_runtimeMemoryTest(JNIEnv* environment, jclass) {
    try { return environment->NewStringUTF(nxtest::run_memory_tests().c_str()); }
    catch (...) { return environment->NewStringUTF("{\"status\":\"FAIL\",\"failures\":[\"Native runtime test exception\"],\"game_runtime_available\":false}"); }
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_gregorioruiz98_fefw_NativeBridge_runtimeCoreTest(JNIEnv* environment, jclass, jstring directory) {
    if (!directory) return environment->NewStringUTF("{\"status\":\"FAIL\",\"game_runtime_available\":false}");
    const char* path=environment->GetStringUTFChars(directory,nullptr);
    if (!path) return nullptr;
    nxtest::set_temporary_directory(path);environment->ReleaseStringUTFChars(directory,path);
    try { return environment->NewStringUTF(nxtest::run_core_tests().c_str()); }
    catch (...) { return environment->NewStringUTF("{\"status\":\"FAIL\",\"game_runtime_available\":false}"); }
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_gregorioruiz98_fefw_NativeBridge_inspectModules(JNIEnv* environment, jclass, jintArray input) {
    if (!input || environment->GetArrayLength(input) != 6)
        return environment->NewStringUTF("{\"status\":\"FAIL\",\"error\":\"Six module descriptors are required\",\"game_runtime_available\":false}");
    std::array<jint, 6> values;
    environment->GetIntArrayRegion(input, 0, 6, values.data());
    if (environment->ExceptionCheck()) return nullptr;
    try {
        const std::array<int, 6> descriptors{values[0], values[1], values[2], values[3], values[4], values[5]};
        return environment->NewStringUTF(nxrt::android_inspect_modules(descriptors).c_str());
    } catch (...) {
        return environment->NewStringUTF("{\"status\":\"FAIL\",\"error\":\"Native module inspection failed\",\"game_runtime_available\":false}");
    }
}
