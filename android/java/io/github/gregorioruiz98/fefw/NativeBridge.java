// SPDX-License-Identifier: GPL-3.0-or-later
package io.github.gregorioruiz98.fefw;

import android.view.Surface;

final class NativeBridge {
    static { System.loadLibrary("fefw_android"); }
    static native String probe();
    static native String renderTest(Surface surface);
    private NativeBridge() {}
}
