#!/bin/sh
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
T=${1:-$R/build/rust-apps}
mkdir -p "$T"
export CARGO_TARGET_DIR=$(cd "$T" && pwd)
export RUSTFLAGS="-C target-feature=+crt-static"
export NVX_RUSTC_VERSION="$(rustc --version)"
cd "$R/rust/rustinfo"
cargo build --release --offline --target x86_64-unknown-linux-gnu
cd "$R/rust/nvxmem"
cargo build --release --offline --target x86_64-unknown-linux-gnu
