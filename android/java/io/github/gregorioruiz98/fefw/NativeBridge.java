// SPDX-License-Identifier: GPL-3.0-or-later
package io.github.gregorioruiz98.fefw;

import android.view.Surface;

final class NativeBridge {
    static final int TARGET_FPS = 30;
    static { System.loadLibrary("fefw_android"); }
    static native String probe();
    static native String runtimeMemoryTest();
    static native String runtimeCoreTest();
    static native String inspectModules(int[] descriptors);
    static native String renderTest(Surface surface, int targetFps);
    private NativeBridge() {}
}
