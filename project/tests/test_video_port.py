import importlib.util
from pathlib import Path
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[1] / 'tools/prepare_video_port.py'


class VideoPortTests(unittest.TestCase):
    def load_tool(self):
        spec = importlib.util.spec_from_file_location('video_port', TOOL)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_relative_configure_preserves_pinned_port_and_companions(self):
        parent = Path.cwd() / 'config-test-results'
        parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='video path spaces ', dir=parent) as temp:
            root = Path(temp)
            port = root / 'vcpkg/ports/libvpx'
            port.mkdir(parents=True)
            original = ('before\n    message(STATUS "Generating makefile")\n'
                        '    vcpkg_execute_required_process(\n'
                        '        COMMAND\n            ${BASH} --noprofile --norc\n'
                        '            "${SOURCE_PATH}/configure"\n'
                        '            --target=${LIBVPX_TARGET_ARCH}-${LIBVPX_TARGET_VS}\n'
                        '        WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-tmp")\n'
                        'after\n# Other platforms keep their configure commands.\n"${SOURCE_PATH}/configure"\n')
            (port / 'portfile.cmake').write_text(original)
            (port / 'vcpkg.json').write_text('{"name":"libvpx","version":"1.16.0"}')
            (port / 'dependency.patch').write_text('synthetic companion fixture\n')
            overlay = self.load_tool().prepare(root / 'vcpkg', root / 'overlays')
            fixed = (overlay / 'libvpx/portfile.cmake').read_text()
            self.assertIn('file(RELATIVE_PATH FEFW_LIBVPX_SOURCE', fixed)
            self.assertIn('"${FEFW_LIBVPX_SOURCE}/configure"', fixed)
            self.assertEqual(fixed.count('"${SOURCE_PATH}/configure"'), 1)
            self.assertEqual((port / 'portfile.cmake').read_text(), original)
            self.assertEqual((overlay / 'libvpx/dependency.patch').read_bytes(), (port / 'dependency.patch').read_bytes())
            self.assertEqual(self.load_tool().prepare(root / 'vcpkg', overlay), overlay)

    def test_unknown_port_is_rejected_before_creating_overlay(self):
        parent = Path.cwd() / 'config-test-results'
        parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as temp:
            root = Path(temp)
            port = root / 'vcpkg/ports/libvpx'
            port.mkdir(parents=True)
            (port / 'portfile.cmake').write_text('unexpected version\n')
            with self.assertRaisesRegex(ValueError, 'configure'):
                self.load_tool().prepare(root / 'vcpkg', root / 'overlay')
            self.assertFalse((root / 'overlay').exists())
