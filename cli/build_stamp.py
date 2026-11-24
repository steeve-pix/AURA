"""Generate a local source build ID using only Python's standard library."""
import sys
import os
from pathlib import Path


def source_files(root):
    files = []
    for directory, extensions in ((root / 'cli', {'.cpp', '.hpp', '.mm', '.py', '.sh'}), (root / 'python', {'.py'})):
        if directory.is_dir():
            for current, children, names in os.walk(directory):
                children[:] = [name for name in children if not (
                    name.startswith('.') or name == '__pycache__' or name == 'build'
                    or name.startswith(('build-', 'cmake-build-')))]
                files.extend(Path(current) / name for name in names
                             if Path(name).suffix in extensions and not name.startswith('.'))
    files.extend(
        path for path in (root / 'cli/CMakeLists.txt', root / 'include/aura/sim/ActionSpec.hpp') if path.is_file())
    return sorted(files, key=lambda path: path.relative_to(root).as_posix())


def fingerprint(root):
    value = 14695981039346656037
    for path in source_files(root):
        for byte in path.relative_to(root).as_posix().encode() + b'\0' + path.read_bytes() + b'\0':
            value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f'{value:016x}'


if __name__ == '__main__':
    root = Path(sys.argv[1]).resolve()
    target = Path(sys.argv[2])
    stamp = fingerprint(root)
    content = f'#pragma once\n#define AURA_CLI_SOURCE_FINGERPRINT "{stamp}"\n#define AURA_CLI_BUILD_ID "{stamp[:8]}"\n'
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_text() != content:
        target.write_text(content)
