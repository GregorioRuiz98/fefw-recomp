// SPDX-License-Identifier: GPL-3.0-or-later
package io.github.gregorioruiz98.fefw;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.hardware.input.InputManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import org.json.JSONObject;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity implements InputManager.InputDeviceListener {
    private static final int PICK_FOLDER = 1, EXPORT_REPORT = 2;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private TextView folderStatus, deviceStatus, controllerStatus, inputStatus;
    TextView renderStatus;
    SurfaceView surface;
    Button renderButton;
    private Uri tree;
    private InputManager inputs;
    private LinearLayout content;
    private boolean busy, verified;
    volatile String nativeReport = "";
    volatile String renderReport = "Not run";

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private TextView text(String value, int size, int color) {
        TextView view = new TextView(this); view.setText(value); view.setTextSize(size); view.setTextColor(color);
        view.setPadding(0, dp(8), 0, dp(8)); content.addView(view); return view;
    }
    private void heading(String value) { text(value, 19, Color.rgb(94, 215, 205)); }
    private Button button(String title, View.OnClickListener action) {
        Button view = new Button(this); view.setText(title); view.setAllCaps(false); view.setOnClickListener(action);
        content.addView(view, new LinearLayout.LayoutParams(-1, dp(52))); return view;
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        ScrollView scroll = new ScrollView(this); scroll.setFillViewport(true); scroll.setBackgroundColor(0xff101822);
        content = new LinearLayout(this); content.setOrientation(LinearLayout.VERTICAL); content.setPadding(dp(22), dp(12), dp(22), dp(24));
        scroll.addView(content); setContentView(scroll);
        scroll.setOnApplyWindowInsetsListener((view, insets) -> {
            android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            view.setPadding(bars.left, bars.top, bars.right, bars.bottom); return insets;
        });
        text("FEFW / ANDROID", 25, 0xfff2e8dc);
        text("Native port • platform diagnostics 0.1.1", 14, 0xffb7c4d0);
        text("This build tests Android files, controllers, memory and Vulkan. It does not run the game yet.", 16, 0xffe8b879);
        heading("Your game folder");
        String saved = getPreferences(MODE_PRIVATE).getString("tree", "");
        if (!saved.isEmpty()) tree = Uri.parse(saved);
        folderStatus = text(tree == null ? "No folder selected. Choose a local folder containing exefs and romfs, or its parent with updates/131072." : "Folder permission saved. Validate to check access and update identity.", 14, 0xffc9d3dc);
        button("Select game folder", v -> {
            if (busy) return;
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, PICK_FOLDER);
        });
        button("Validate compatible copy", v -> validateFolder());
        heading("Device and native platform");
        ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
        ((ActivityManager)getSystemService(ACTIVITY_SERVICE)).getMemoryInfo(memory);
        text(Build.MANUFACTURER + " " + Build.MODEL + " • Android " + Build.VERSION.RELEASE + "\n" + String.format(Locale.ROOT, "%.1f GiB system RAM", memory.totalMem / 1073741824.0), 14, 0xffc9d3dc);
        deviceStatus = text("Checking native library…", 14, 0xffc9d3dc);
        button("Refresh native checks", v -> probe());
        surface = new SurfaceView(this); content.addView(surface, new LinearLayout.LayoutParams(-1, dp(150)));
        renderStatus = text("30 FPS target (33.33 ms per frame). The Vulkan colour test measures submission pacing; it does not measure game performance.", 14, 0xffc9d3dc);
        renderButton = button("Test 30 FPS Vulkan pacing", v -> render());
        heading("Controller");
        controllerStatus = text("Scanning…", 14, 0xffc9d3dc);
        inputStatus = text("Connect a USB or Bluetooth controller and press a button or move a stick.", 14, 0xffc9d3dc);
        button("Rescan controllers", v -> scanControllers());
        heading("Development status and credits");
        text("Next: port Win32 memory aliases, threads, filesystem, audio and the NVN/Vulkan game renderer; compile the lifted game for ARM64. A matching game folder does not make this build playable.", 14, 0xffc9d3dc);
        text("Runtime foundation: Goldoire, drakolordx7 and featureJosh. Patch reference: Kompas9. Android integration: GregorioRuiz98, with Codex assistance. Woven Shield is original project artwork. No game files are included.", 13, 0xffb7c4d0);
        button("Export device report", v -> {
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT).setType("application/json").putExtra(Intent.EXTRA_TITLE, "fefw-android-diagnostics.json");
            startActivityForResult(intent, EXPORT_REPORT);
        });
        inputs = (InputManager)getSystemService(INPUT_SERVICE); probe();
    }

    private void probe() {
        worker.execute(() -> {
            try {
                nativeReport = NativeBridge.probe(); JSONObject data = new JSONObject(nativeReport);
                String message = "ABI: " + data.getString("abi") + " • host page: " + data.getInt("page_size") + " bytes\n"
                    + "GPU: " + data.optString("gpu", "unavailable") + " • Vulkan " + data.optString("vulkan", "unavailable")
                    + "\nShared memory alias: " + data.getBoolean("shared_memory_alias") + " • guest main-address page: " + data.getBoolean("main_address_page")
                    + "\nVulkan 1.3: " + data.optBoolean("vulkan_1_3") + " • device addresses: " + data.optBoolean("buffer_device_address")
                    + "\nHost memory import: " + data.optBoolean("external_memory_host") + " • game runtime: pending";
                runOnUiThread(() -> deviceStatus.setText(message));
            } catch (Throwable error) { runOnUiThread(() -> deviceStatus.setText("Native check failed: " + error.getClass().getSimpleName())); }
        });
    }

    private void render() {
        if (busy || !surface.getHolder().getSurface().isValid()) return;
        busy = true; renderButton.setEnabled(false); renderReport = "Not run";
        renderStatus.setText("Presenting 120 diagnostic frames with a 30 FPS limit…");
        worker.execute(() -> {
            String message;
            try {
                renderReport = NativeBridge.renderTest(surface.getHolder().getSurface(), NativeBridge.TARGET_FPS);
                if (renderReport.startsWith("FAILED:")) message = renderReport;
                else {
                    JSONObject data = new JSONObject(renderReport);
                    message = String.format(Locale.ROOT,
                        "%d diagnostic frames • target %d FPS\nSubmission rate %.2f FPS • mean %.2f ms • p95 %.2f ms\nColour test only. The Android game engine is still pending.",
                        data.getInt("frames"), data.getInt("target_fps"), data.getDouble("submission_fps"),
                        data.getDouble("mean_interval_ms"), data.getDouble("p95_interval_ms"));
                }
            }
            catch (Throwable error) { message = renderReport = "FAILED: " + error.getClass().getSimpleName(); }
            final String status = message;
            runOnUiThread(() -> { busy = false; renderButton.setEnabled(true); renderStatus.setText(status); });
        });
    }

    private void validateFolder() {
        if (busy) return;
        if (tree == null) { folderStatus.setText("Select a folder first."); return; }
        busy = true; verified = false; final Uri selection = tree;
        worker.execute(() -> {
            String result;
            try {
                result = GameFolder.validate(this, selection, message -> runOnUiThread(() -> folderStatus.setText(message)));
                verified = true;
            } catch (SecurityException error) { result = "Folder permission expired. Select your folder again."; }
            catch (Exception error) { result = "Validation failed: " + error.getMessage(); }
            final String message = result;
            runOnUiThread(() -> { busy = false; folderStatus.setText(message); });
        });
    }

    private static boolean gamepad(InputDevice device) {
        return device != null && ((device.getSources() & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
            || (device.getSources() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK);
    }
    private void scanControllers() {
        StringBuilder list = new StringBuilder();
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (gamepad(device)) list.append(device.getName()).append(device.getVendorId() == 0x054c ? " • PlayStation" : "").append('\n');
        }
        controllerStatus.setText(list.length() == 0 ? "No controller detected" : list.toString().trim());
    }
    @Override public void onInputDeviceAdded(int id) { scanControllers(); }
    @Override public void onInputDeviceChanged(int id) { scanControllers(); }
    @Override public void onInputDeviceRemoved(int id) { scanControllers(); inputStatus.setText("Device list changed. Press a button on the active controller."); }
    @Override protected void onResume() { super.onResume(); if (inputs != null) { inputs.registerInputDeviceListener(this, null); scanControllers(); } }
    @Override protected void onPause() { if (inputs != null) inputs.unregisterInputDeviceListener(this); super.onPause(); }
    @Override protected void onDestroy() { worker.shutdownNow(); super.onDestroy(); }
    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (gamepad(event.getDevice()) && KeyEvent.isGamepadButton(event.getKeyCode())) {
            String symbol = KeyEvent.keyCodeToString(event.getKeyCode());
            if (event.getDevice().getVendorId() == 0x054c) {
                switch (event.getKeyCode()) {
                    case KeyEvent.KEYCODE_BUTTON_A: symbol = "✕ Cross (bottom)"; break;
                    case KeyEvent.KEYCODE_BUTTON_B: symbol = "○ Circle (right)"; break;
                    case KeyEvent.KEYCODE_BUTTON_X: symbol = "□ Square (left)"; break;
                    case KeyEvent.KEYCODE_BUTTON_Y: symbol = "△ Triangle (top)"; break;
                }
            }
            inputStatus.setText(symbol + (event.getAction() == KeyEvent.ACTION_UP ? " • released" : " • pressed")); return true;
        }
        return super.dispatchKeyEvent(event);
    }
    @Override public boolean onGenericMotionEvent(MotionEvent event) {
        if (gamepad(event.getDevice()) && event.getAction() == MotionEvent.ACTION_MOVE) {
            inputStatus.setText(String.format(Locale.ROOT, "L %.2f, %.2f  R %.2f, %.2f\nTriggers %.2f / %.2f", event.getAxisValue(MotionEvent.AXIS_X), event.getAxisValue(MotionEvent.AXIS_Y), event.getAxisValue(MotionEvent.AXIS_Z), event.getAxisValue(MotionEvent.AXIS_RZ), event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_RTRIGGER))); return true;
        }
        return super.onGenericMotionEvent(event);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        if (request == PICK_FOLDER) {
            try {
                getContentResolver().takePersistableUriPermission(data.getData(), Intent.FLAG_GRANT_READ_URI_PERMISSION);
                tree = data.getData(); getPreferences(MODE_PRIVATE).edit().putString("tree", tree.toString()).apply();
                verified = false; folderStatus.setText("Folder selected. Validate to check the six modules.");
            } catch (SecurityException error) { folderStatus.setText("Cannot keep this provider's permission. Choose a local folder."); }
        } else if (request == EXPORT_REPORT) {
            final Uri output = data.getData();
            worker.execute(() -> {
                try {
                    JSONObject report = new JSONObject(); report.put("kind", "android-platform-diagnostics");
                    report.put("model", Build.MODEL); report.put("android_api", Build.VERSION.SDK_INT);
                    report.put("platform", nativeReport.isEmpty() ? JSONObject.NULL : new JSONObject(nativeReport));
                    report.put("presentation", renderReport); report.put("modules_verified", verified);
                    report.put("target_fps", NativeBridge.TARGET_FPS);
                    report.put("game_runtime_available", false);
                    try (java.io.OutputStream stream = getContentResolver().openOutputStream(output, "wt")) {
                        if (stream == null) throw new java.io.IOException();
                        stream.write(report.toString(2).getBytes(java.nio.charset.StandardCharsets.UTF_8));
                    }
                    runOnUiThread(() -> renderStatus.setText("Report saved. It contains no folder URI, game files or device serial."));
                } catch (Exception error) { runOnUiThread(() -> renderStatus.setText("Report could not be saved.")); }
            });
        }
    }
}
