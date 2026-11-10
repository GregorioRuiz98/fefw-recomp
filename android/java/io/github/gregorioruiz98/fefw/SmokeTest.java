// SPDX-License-Identifier: GPL-3.0-or-later
// On-device checks using synthetic headers and the actual native Android library.
package io.github.gregorioruiz98.fefw;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import java.io.File;
import org.json.JSONObject;

public final class SmokeTest extends Instrumentation {
    private int checks;
    private boolean testLocalModules;
    private void check(boolean value, String label) {
        ++checks; android.util.Log.i("FEFWTests", "Check " + checks + ": " + label + " = " + value); if (!value) throw new AssertionError(label);
    }
    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        testLocalModules = arguments != null && "1".equals(arguments.getString("test_local_modules"));
        start();
    }
    @Override public void onStart() {
        Bundle result = new Bundle(); MainActivity activity = null;
        try {
            byte[] header = new byte[256]; header[0]='N'; header[1]='S'; header[2]='O'; header[3]='0';
            for (int i=0;i<32;++i) header[0x40+i]=(byte)(i+1);
            String expected=GameFolder.hex(header,0x40,32);
            check(GameFolder.headerMatches(header,expected),"valid synthetic NSO identity");
            header[0]='X'; check(!GameFolder.headerMatches(header,expected),"invalid magic rejected");
            header[0]='N'; header[0x40]=0; check(!GameFolder.headerMatches(header,expected),"wrong Build ID rejected");
            check(!GameFolder.headerMatches(new byte[40],expected),"truncated header rejected");
            JSONObject probe = new JSONObject(NativeBridge.probe());
            check(!probe.getBoolean("game_runtime_available"),"unfinished engine is reported honestly");
            check(probe.getInt("page_size") >= 4096,"native host page size");
            check(probe.getBoolean("shared_memory_alias"),"real shared aliases observe the same data");
            check(probe.getInt("gpu_count") > 0,"Vulkan device enumerated");
            activity=(MainActivity)startActivitySync(new Intent(getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            final MainActivity screen=activity;
            NativeBridge.setRuntimeFocus(false);
            check(!NativeBridge.runtimeInFocus(),"Android background state reaches applet manager");
            NativeBridge.setRuntimeFocus(true);
            check(NativeBridge.runtimeInFocus(),"Android foreground state reaches applet manager");
            for(int i=0;i<100 && screen.nativeReport.isEmpty();++i) Thread.sleep(100);
            check(!screen.nativeReport.isEmpty(),"activity loads JNI diagnostics");
            waitForIdleSync();
            runOnMainSync(() -> screen.memoryButton.performClick());
            for(int i=0;i<300 && screen.memoryReport.equals("Not run");++i) Thread.sleep(100);
            JSONObject memory = new JSONObject(screen.memoryReport);
            check(memory.getString("status").equals("PASS"),"Android runtime memory: "+screen.memoryReport);
            check(memory.getBoolean("memory_backend_available") && memory.getInt("cases_passed")==33 && memory.getInt("cases_failed")==0,"all upstream and Android memory cases pass inside app sandbox");
            check(memory.getBoolean("cleanup_verified") && !memory.getBoolean("game_runtime_available"),"memory integration cleans up and does not claim a complete engine");
            waitForIdleSync();
            runOnMainSync(() -> screen.coreButton.performClick());
            for(int i=0;i<1800 && screen.coreReport.equals("Not run");++i) Thread.sleep(100);
            JSONObject core = new JSONObject(screen.coreReport);
            check(core.getString("status").equals("PASS"),"Android module core: "+screen.coreReport);
            check(core.getInt("abi_version")==3 && core.getInt("cases_passed")==253 && core.getInt("cases_failed")==0,"all execution, guest thread, synchronization, IPC and logger cases pass in app sandbox");
            check(core.getBoolean("bootstrap_kernel_available") && core.getBoolean("guest_worker_threads_available") && core.getBoolean("ipc_transport_available") && core.getBoolean("log_manager_available") && core.getBoolean("applet_services_available") && core.getBoolean("time_services_available"),"thread, IPC and logger capabilities are integrated");
            check(core.getBoolean("cleanup_verified") && !core.getBoolean("game_runtime_available"),"module core does not claim a playable engine");
            JSONObject missing = new JSONObject(NativeBridge.inspectModules(new int[]{-1,-1,-1,-1,-1,-1}));
            check(missing.getString("status").equals("FAIL") && missing.getBoolean("cleanup_verified"),"invalid file descriptors fail with cleanup");
            if (testLocalModules) {
                // Opt-in local development test: files are supplied separately and never packaged.
                File directory = new File(getTargetContext().getExternalFilesDir(null), "loader-inputs");
                String[] names = {"main", "rtld", "sdk", "subsdk0", "subsdk1", "subsdk2"};
                ParcelFileDescriptor[] opened = new ParcelFileDescriptor[6]; int[] descriptors = new int[6];
                try {
                    for (int i=0; i<names.length; ++i) {
                        opened[i] = ParcelFileDescriptor.open(new File(directory, names[i]), ParcelFileDescriptor.MODE_READ_ONLY);
                        descriptors[i] = opened[i].getFd();
                    }
                    String report = NativeBridge.inspectModules(descriptors);
                    JSONObject loaded = new JSONObject(report);
                    check(loaded.getString("status").equals("PASS") && loaded.getInt("modules_loaded")==6,"six real local modules: "+report);
                    check(loaded.getBoolean("cleanup_verified"),"real module mappings released after test");
                    check(!loaded.getBoolean("guest_code_executed") && !loaded.getBoolean("game_runtime_available"),"loading data is not game execution");
                    result.putString("local_game_modules",report);
                } finally { for(ParcelFileDescriptor descriptor:opened) if(descriptor!=null) descriptor.close(); }
            }
            waitForIdleSync();
            for(int i=0;i<100 && !screen.surface.getHolder().getSurface().isValid();++i) Thread.sleep(100);
            check(NativeBridge.renderTest(screen.surface.getHolder().getSurface(), 0).startsWith("FAILED:"),"zero FPS rejected before dividing or rendering");
            runOnMainSync(() -> screen.renderButton.performClick());
            for(int i=0;i<300 && screen.renderReport.equals("Not run");++i) Thread.sleep(100);
            JSONObject pacing = new JSONObject(screen.renderReport);
            check(pacing.getString("status").equals("PASS"),"real Vulkan presentation: "+screen.renderReport);
            check(pacing.getInt("target_fps")==30 && pacing.getInt("frames")==120,"30 FPS target reaches native loop");
            check(pacing.getDouble("submission_span_ms")>=3900,"120 frames are paced instead of submitted at full speed");
            check(pacing.getDouble("submission_fps")<=30.1,"monotonic pacing stays within the average frame budget");
            check(pacing.getDouble("submission_fps")>=28.5,"colour test reaches the 30 FPS target within five percent");
            check(!pacing.getBoolean("game_runtime_available"),"colour-test timing is not advertised as gameplay");
            result.putString("status","PASS"); result.putInt("checks",checks);
            result.putString("native",probe.toString()); result.putString("presentation",screen.renderReport);
            result.putString("runtime_memory", screen.memoryReport);
            result.putString("runtime_core", screen.coreReport);
            result.putBoolean("game_runtime_available",false);
            finish(Activity.RESULT_OK,result);
        } catch(Throwable error) {
            result.putString("status","FAIL");result.putInt("checks",checks);result.putString("error",error.toString());
            finish(Activity.RESULT_CANCELED,result);
        } finally {
            if(activity!=null) { final Activity screen=activity;runOnMainSync(screen::finish); }
        }
    }
}
