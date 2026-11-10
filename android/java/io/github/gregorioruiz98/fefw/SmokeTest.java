// SPDX-License-Identifier: GPL-3.0-or-later
// On-device checks using synthetic headers and the actual native Android library.
package io.github.gregorioruiz98.fefw;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.Bundle;
import org.json.JSONObject;

public final class SmokeTest extends Instrumentation {
    private int checks;
    private void check(boolean value, String label) {
        ++checks; if (!value) throw new AssertionError(label);
    }
    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }
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
