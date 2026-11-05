#!/bin/sh
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
T=${1:-$R/build/rust}
mkdir -p "$T"
export CARGO_TARGET_DIR=$(cd "$T" && pwd)
export RUSTFLAGS="-C no-redzone=yes -C relocation-model=static -C code-model=small -C panic=abort -C target-feature=+sse2"
cd "$R/rust/nvxlayout"
cargo build --release --offline
A=$CARGO_TARGET_DIR/release/libnvxlayout.a
[ -f "$A" ]
python3 "$R/tools/rust_check.py" "$A"
