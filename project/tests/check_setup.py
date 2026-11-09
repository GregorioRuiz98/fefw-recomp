"""Exercise the real standalone configurator against a private local copy."""
import argparse
import configparser
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--work', required=True, type=Path)
    p.add_argument('--source', required=True, type=Path)
    p.add_argument('--report', required=True, type=Path)
    p.add_argument('--boot', action='store_true')
    a = p.parse_args()
    work = a.work.resolve()
    folder = work / 'tests' / 'setup-integration-á'
    folder.mkdir(parents=True, exist_ok=True)
    build = work / 'build/fefw'
    for name in ['fefw.exe', 'fefw_setup.exe', 'astcdec-avx2-shared.dll', 'astcdec-sse2-shared.dll', 'astcdec-sse4.1-shared.dll']:
        shutil.copy2(build / name, folder / name)
    shutil.copytree(build / 'fonts', folder / 'fonts', dirs_exist_ok=True)
    for name in ['pipeline_cache.bin', 'pipeline_keys.bin']:
        src = work / 'Fire Emblem Recomp' / name
        if src.exists():
            shutil.copy2(src, folder / name)
    game_ini = folder / 'fefw.ini'
    (folder / 'fefw.features').write_text('fefw-runtime-8', encoding='utf-8')
    control_ini = folder / 'controls.ini'
    game_ini.write_text(
        '[fs]\nexefs = ' + str(work / 'prepared/exefs') + '\nromfs = ' + str(a.source / 'romfs') +
        '\nsave_dir = save/my-existing-save\n[boot]\ngfx = window\nmax_seconds = 0\n[nvn]\nshader_cache = ' +
        str(work / 'shaders/validated-sdk22') + '\ndlss_spatial_fallback = 1\n[audio]\nrevision16_probe = 1\n'
        '[title]\naccount_save_size = 67108864\n[play]\nservice_input_fallback = 1\n[diag]\nwatchdog_seconds = -1\n', encoding='utf-8')
    control_ini.write_text('[display]\nwidth = 1280\nheight = 720\n[padmap]\nstart = PLUS\n[bindings.all]\nP = btn_plus\n', encoding='utf-8')
    results = []
    flags = subprocess.CREATE_NO_WINDOW

    def run(name, args, expected=0):
        r = subprocess.run([str(folder / 'fefw_setup.exe'), '--game', str(folder)] + args,
                           capture_output=True, creationflags=flags, timeout=60)
        output = (r.stdout + r.stderr).decode('utf-8', errors='replace')
        results.append({'name': name, 'exit_code': r.returncode, 'expected': expected, 'passed': r.returncode == expected, 'output': output.strip()})
        if r.returncode != expected:
            raise AssertionError((name, r.returncode, output))
        return output

    def read(path):
        c = configparser.ConfigParser(interpolation=None)
        c.read(path, encoding='utf-8-sig')
        return c

    run('source_identity', ['--validate', str(a.source)])
    run('first_run_requires_setup', ['--status'], 2)
    before = game_ini.read_bytes(), control_ini.read_bytes()
    ui = run('real_ui_and_cancel', ['--test-ui'])
    assert 'ui_valid=1' in ui and 'ui_checks=' in ui
    assert before == (game_ini.read_bytes(), control_ini.read_bytes())
    assert (folder / 'setup-ui.bmp').stat().st_size > 100000
    for state in ('game', 'graphics', 'startup', 'empty', 'compact', 'dpi144', 'preparing', 'updates', 'performance', 'window', 'controllers', 'audio', 'tools', 'spanish'):
        assert (folder / f'setup-ui-{state}.bmp').stat().st_size > 100000
    run('save_show_setup', ['--apply', '--source', str(a.source), '--width', '1600', '--height', '900', '--language', 'Spanish', '--skip', '0', '--vsync', '1'])
    text = run('show_setup_preference', ['--status'])
    assert 'ready=1' in text and 'show_setup=1' in text
    game, controls = read(game_ini), read(control_ini)
    assert game['system']['language'] == 'Spanish' and 'Spanish' in game['title']['supported_languages']
    assert game['fs']['save_dir'] == 'save/my-existing-save'
    assert controls['padmap']['start'] == 'PLUS' and controls['bindings.all']['P'] == 'btn_plus'
    assert controls['display']['width'] == '1600' and controls['display']['height'] == '900'
    assert controls['display']['vsync'] == '1'
    run('save_skip_setup', ['--apply', '--source', str(a.source), '--skip', '1'])
    text = run('direct_launch_preference', ['--status'])
    assert 'show_setup=0' in text
    run('enable_fps60', ['--apply', '--fps60', '1'])
    assert read(game_ini)['mods']['fps60'] == '1'
    assert read(control_ini)['display']['fps_limit'] == '60'
    assert 'fps60=1' in run('fps60_status', ['--status'])
    run('fps60_checked_ui', ['--test-ui'])
    run('enable_fps120', ['--apply', '--fps', '120', '--output', '1920x1080', '--render-ratio', '0.667', '--window-mode', 'borderless'])
    assert read(game_ini)['mods']['fps120'] == '1' and read(game_ini)['mods']['fps60'] == '0'
    assert read(game_ini)['fefw']['output'] == '1920x1080' and read(control_ini)['display']['window_mode'] == 'borderless'
    assert read(control_ini)['display']['fps_limit'] == '120'
    run('fps120_and_render_ui', ['--test-ui'])
    before_invalid = game_ini.read_bytes(), control_ini.read_bytes()
    run('invalid_render_output', ['--apply', '--output', '8000x4000'], 2)
    run('invalid_render_ratio', ['--apply', '--render-ratio', '2'], 2)
    run('invalid_fps', ['--apply', '--fps', '90'], 2)
    assert before_invalid == (game_ini.read_bytes(), control_ini.read_bytes())
    run('original_render_and_fps', ['--apply', '--fps', '0', '--output', 'console', '--render-ratio', '0', '--window-mode', 'windowed'])
    assert read(control_ini)['display']['fps_limit'] == '60' and read(game_ini)['mods']['fps120'] == '0'
    run('disable_fps60', ['--apply', '--fps60', '0'])
    assert read(game_ini)['mods']['fps60'] == '0'
    assert read(game_ini)['dlss']['upscaler'] == 'taau'
    run('select_bilinear', ['--apply', '--upscaler', 'bilinear'])
    assert read(game_ini)['dlss']['upscaler'] == 'bilinear'
    run('bilinear_selected_ui', ['--test-ui'])
    before = game_ini.read_bytes(), control_ini.read_bytes()
    run('invalid_upscaler_preserves_configuration', ['--apply', '--upscaler', 'invalid'], 2)
    assert before == (game_ini.read_bytes(), control_ini.read_bytes())
    run('select_temporal', ['--apply', '--upscaler', 'taau'])
    assert read(game_ini)['dlss']['upscaler'] == 'taau'
    run('audio_controller_and_interface_preferences', ['--apply', '--volume', '0.35', '--mute-unfocused', '1', '--deadzone', '0.2', '--right-deadzone', '0.25', '--ui-language', 'es'])
    assert read(game_ini)['launcher']['ui_language'] == 'es' and read(game_ini)['system']['language'] == 'Spanish'
    assert read(game_ini)['audio']['volume'] == read(control_ini)['audio']['volume'] == '0.350000'
    assert read(control_ini)['audio']['mute_unfocused'] == '1' and read(control_ini)['pad']['right_deadzone'] == '0.250000'
    run('spanish_interface_restores_preferences', ['--test-ui'])
    before_preferences = game_ini.read_bytes(), control_ini.read_bytes()
    for option, value in (('--volume', 'nan'), ('--deadzone', '-0.1'), ('--right-deadzone', '0.6'), ('--ui-language', 'unknown')):
        run('reject_' + option.removeprefix('--'), ['--apply', option, value], 2)
    assert before_preferences == (game_ini.read_bytes(), control_ini.read_bytes())
    run('restore_interface_defaults', ['--apply', '--volume', '1', '--mute-unfocused', '0', '--deadzone', '0.1', '--right-deadzone', '0.1', '--ui-language', 'en'])
    assert 'controller_valid=1' in run('standalone_controller_hotplug', ['--test-controllers'])
    (folder / 'fefw.features').unlink()
    run('old_executable_requires_update', ['--status'], 2)
    (folder / 'fefw.features').write_text('fefw-runtime-5', encoding='utf-8')
    run('old_shader_cache_requires_update', ['--status'], 2)
    (folder / 'fefw.features').write_text('fefw-runtime-8', encoding='utf-8')
    run('standalone_ui_ignores_skip', ['--test-ui'])
    before = game_ini.read_bytes(), control_ini.read_bytes()
    run('unsupported_language_keeps_configuration', ['--apply', '--language', 'Portuguese'], 2)
    assert before == (game_ini.read_bytes(), control_ini.read_bytes())
    run('invalid_dimensions_keep_configuration', ['--apply', '--width', '12'], 2)
    assert before == (game_ini.read_bytes(), control_ini.read_bytes())
    run('missing_source_keeps_configuration', ['--apply', '--source', str(folder / 'missing')], 2)
    assert before == (game_ini.read_bytes(), control_ini.read_bytes())
    # An inaccessible game ini must roll back a successfully written controls ini.
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.CreateFileW(str(game_ini), 0x80000000, 1, None, 3, 0, None)
    assert handle not in {None, ctypes.c_void_p(-1).value}
    try:
        run('failed_save_rolls_back_both_files', ['--apply', '--width', '1280'], 2)
    finally:
        kernel.CloseHandle(handle)
    assert before == (game_ini.read_bytes(), control_ini.read_bytes())
    text = game_ini.read_text(encoding='utf-8').replace(str(a.source / 'romfs'), str(folder / 'moved-romfs'))
    game_ini.write_text(text, encoding='utf-8')
    text = run('moved_resources_reopen_setup', ['--status'], 2)
    assert 'show_setup=1' in text
    game_ini.write_bytes(before[0])
    if a.boot:
        env = os.environ.copy()
        env.update(NXRT_GFX_HIDDEN='1', NXRT_BOOT_SCREENSHOT_AT='28')
        # No --skip-setup: the persisted preference must take this path itself.
        r = subprocess.run([str(folder / 'fefw.exe'), '--max-seconds', '36'], env=env,
                           creationflags=flags, capture_output=True, timeout=65)
        log = (folder / 'fefw.log').read_text(encoding='utf-8', errors='replace')
        png = (folder / 'shot_0.png').read_bytes()
        ok = (r.returncode == 124 and 'services: language es' in log and '1600x900' in log and 'present mode fifo' in log
              and struct.unpack('>II', png[16:24]) == (1600, 900) and 'GUEST TRAP' not in log and 'DEVICE_LOST' not in log)
        results.append({'name': 'direct_native_boot_spanish_1600x900', 'passed': ok, 'exit_code': r.returncode})
        assert ok, 'Configured native boot did not pass; inspect the private log'
    a.report.write_text(json.dumps({'passed': all(x['passed'] for x in results), 'checks': results,
        'executable_sha256': hashlib.file_digest((folder / 'fefw.exe').open('rb'), 'sha256').hexdigest(),
        'original_files_modified': False}, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(json.dumps({'passed': True, 'checks': len(results), 'boot': a.boot}))


if __name__ == '__main__':
    main()
