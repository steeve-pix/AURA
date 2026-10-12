#!/bin/sh
set -eu

case "${1:-}" in
  ""|--iterm2) ;;
  *) echo "Usage: $0 [--iterm2]" >&2; exit 2 ;;
esac

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${AURA_BUILD_DIR:-"$project_dir/cli/build"}
bin_dir=${AURA_BIN_DIR:-"$HOME/.local/bin"}

cmake -S "$project_dir/cli" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --target aura_cli
mkdir -p "$bin_dir"
install_file=$(mktemp "$bin_dir/.aura-cli.XXXXXX")
trap 'rm -f "$install_file"' EXIT HUP INT TERM
cp "$build_dir/aura-cli" "$install_file"
chmod 755 "$install_file"
mv -f "$install_file" "$bin_dir/aura-cli"

if [ -d "$build_dir/AuraDictation.app" ]; then
  helper_dir=$(CDPATH= cd -- "$bin_dir/.." && pwd)/lib/aura-cli
  mkdir -p "$helper_dir"
  /usr/bin/ditto "$build_dir/AuraDictation.app" "$helper_dir/AuraDictation.app"
fi

case ":${PATH}:" in
  *":$bin_dir:"*) echo "Installed. Run 'aura-cli' from any folder." ;;
  *)
    echo "Installed to $bin_dir/aura-cli. Add this to ~/.zshrc:"
    echo "  export PATH=\"$bin_dir:\$PATH\""
    ;;
esac

if [ "${1:-}" = "--iterm2" ]; then
  python3 "$project_dir/cli/install-iterm2-profile.py" "$bin_dir/aura-cli"
fi
