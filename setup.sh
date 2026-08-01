#!/bin/bash
#
# setup.sh — Build ZRay from a fresh checkout.
#
# Runs the full build sequence:
#   1. Locate a stock LLVM 15 install
#   2. Source setupEnv.sh so the makefiles find clang/opt/etc.
#   3. Build the ZRay pass + runtime into ./bin
#
# Run from the repo root:  ./setup.sh
# Use a non-default LLVM:  LLVM_BIN=/path/to/llvm/bin ./setup.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

log() { printf '\n\033[1;34m==> %s\033[0m\n' "$1"; }

# --- 1. LLVM ---------------------------------------------------------------
# ZRay builds against a stock LLVM 15 (no custom fork required). Install it via
# your distro, e.g. on Debian/Ubuntu:
#   sudo apt install llvm-15-dev clang-15
# and point LLVM_BIN at its bin/ directory if it is not /usr/lib/llvm-15/bin.
LLVM_BIN="${LLVM_BIN:-/usr/lib/llvm-15/bin}"
if [[ ! -x "${LLVM_BIN}/llvm-config" ]]; then
    echo "ERROR: no llvm-config at ${LLVM_BIN}/llvm-config." >&2
    echo "Install LLVM 15 (e.g. 'sudo apt install llvm-15-dev clang-15') or set LLVM_BIN." >&2
    exit 1
fi
log "Using LLVM at ${LLVM_BIN} ($(${LLVM_BIN}/llvm-config --version))"
export LLVM_BIN

# --- 2. Environment -------------------------------------------------------
log "Sourcing setupEnv.sh (sets CUSTOM_CC/OPT/LINK, ZRAY_BIN_PATH, ...)"
# setupEnv.sh references optional vars like $HOST that may be unset; relax
# nounset just for the source so it doesn't trip our strict mode.
set +u
# shellcheck disable=SC1091
source ./setupEnv.sh
set -u

# --- 3. ZRay pass + runtime ----------------------------------------------
log "Building the ZRay pass and runtime (make zray -> ./bin)"
make zray

log "Done. Artifacts in ./bin:"
ls -1 bin 2>/dev/null || true

cat <<'EOF'

Next steps:
  * Every new shell that builds ZRAY or benchmarks must first:  source setupEnv.sh
  * Build example targets:   make examples
  * gem5 variants:           make examples_gem5  /  make examples_gem5_zray
EOF
