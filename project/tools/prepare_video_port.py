"""Create a local overlay for the pinned libvpx port; leave vcpkg untouched."""
import argparse
from pathlib import Path
import shutil


def prepare(vcpkg: Path, destination: Path) -> Path:
    port = vcpkg.resolve() / 'ports/libvpx'
    original = (port / 'portfile.cmake').read_text(encoding='utf-8')
    configure = ('"${SOURCE_PATH}/configure"\n'
                 '            --target=${LIBVPX_TARGET_ARCH}-${LIBVPX_TARGET_VS}')
    marker = '    message(STATUS "Generating makefile")'
    if original.count(configure) != 1:
        raise ValueError('Unexpected libvpx configure command in the pinned port.')
    # configure stores its invocation path in SRC_PATH_BARE, which GNU make
    # splits on spaces. Source and build directories share the libvpx tree,
    # so a relative invocation avoids spaces without moving any user files.
    declaration = ('    file(RELATIVE_PATH FEFW_LIBVPX_SOURCE\n'
                   '        "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-tmp" "${SOURCE_PATH}")\n')
    if original.count(marker) != 1:
        raise ValueError('Unexpected libvpx configure block in the pinned port.')
    patched = original.replace(marker, declaration + marker, 1)
    patched = patched.replace(configure, configure.replace('${SOURCE_PATH}', '${FEFW_LIBVPX_SOURCE}'), 1)
    destination = destination.resolve()
    if destination == port or destination.is_relative_to(port):
        raise ValueError('The overlay must be outside the original libvpx port.')
    shutil.copytree(port, destination / 'libvpx', dirs_exist_ok=True)
    (destination / 'libvpx/portfile.cmake').write_text(patched, encoding='utf-8', newline='\n')
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vcpkg', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(prepare(args.vcpkg, args.out))
