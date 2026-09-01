#!/usr/bin/env bash
set -euo pipefail

test_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd "$test_dir/.." && pwd)
input_dir="$test_dir/inputs"
llvm_bin=${LLVM_BIN:-/usr/lib/llvm-15/bin}

find_tool() {
  local name=$1
  local versioned_name=$2

  if [[ -x "$llvm_bin/$name" ]]; then
    printf '%s\n' "$llvm_bin/$name"
  elif command -v "$versioned_name" >/dev/null 2>&1; then
    command -v "$versioned_name"
  elif command -v "$name" >/dev/null 2>&1; then
    command -v "$name"
  else
    printf 'error: could not find %s (set LLVM_BIN to the LLVM 15 bin directory)\n' "$name" >&2
    return 1
  fi
}

cxx=$(find_tool clang++ clang++-15)
opt=$(find_tool opt opt-15)
llvm_link=$(find_tool llvm-link llvm-link-15)

if ! "$opt" --version | grep -q 'LLVM version 15\.'; then
  printf 'error: regression tests require LLVM 15; found: %s\n' \
    "$("$opt" --version | head -n 1)" >&2
  exit 1
fi

plugin="$repo_dir/bin/libzray.so"
runtime="$repo_dir/bin/zray_runtime.ll"
if [[ ! -f "$plugin" || ! -f "$runtime" ]]; then
  printf 'error: ZRay is not built; run make -C %s zray first\n' "$repo_dir" >&2
  exit 1
fi

tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/zray-regressions.XXXXXX")
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

fail() {
  printf 'FAIL: %s\n' "$1" >&2
  exit 1
}

pass() {
  printf 'PASS: %s\n' "$1"
}

# Regions in different loop nests must not share a counter merely because their
# blocks are mutually control-dependent. The fixed pass emits five counter
# sites for this input; the pre-fix pass emitted three.
"$cxx" -O0 -Xclang -disable-O0-optnone -I "$repo_dir/include" \
  -S -emit-llvm "$input_dir/nested-loop.cc" -o "$tmp_dir/nested-loop.ll"
ZRAY_LOGFILE="$tmp_dir/nested-loop.zlog" \
  "$opt" -enable-new-pm=0 -mem2reg -load "$plugin" -zray \
  --loophoist=false --postdomset=true -S "$tmp_dir/nested-loop.ll" \
  -o "$tmp_dir/nested-loop.instrumented.ll" 2>"$tmp_dir/nested-loop.pass.log"

counter_sites=$(grep -c 'call void @_Z21incrementCounterArraymm' \
  "$tmp_dir/nested-loop.instrumented.ll" || true)
[[ "$counter_sites" -eq 5 ]] || \
  fail "cross-loop control equivalence: expected 5 counter sites, found $counter_sites"
pass "cross-loop control equivalence keeps counters separate"

# Full-scan mode must close a region on every return. Running both paths also
# verifies that the runtime sees a balanced region after the second call.
ZRAY_LOGFILE="$tmp_dir/full-scan.zlog" \
  "$opt" -enable-new-pm=0 -load "$plugin" -zray --full-scan \
  --functionclone=false -S "$input_dir/full-scan-multiple-exits.ll" \
  -o "$tmp_dir/full-scan.instrumented.ll" 2>"$tmp_dir/full-scan.pass.log"

branchy_end_sites=$(awk '
  /^define i32 @branchy/ { in_function = 1 }
  in_function && /call void @_Z14endTimingEventm/ { count++ }
  in_function && /^}/ { print count + 0; exit }
' "$tmp_dir/full-scan.instrumented.ll")
[[ "$branchy_end_sites" -eq 2 ]] || \
  fail "full-scan exits: expected 2 end sites in branchy, found $branchy_end_sites"

"$opt" -verify -disable-output "$tmp_dir/full-scan.instrumented.ll"
"$llvm_link" "$tmp_dir/full-scan.instrumented.ll" "$runtime" \
  -S -o "$tmp_dir/full-scan.linked.ll"
"$cxx" "$tmp_dir/full-scan.linked.ll" -pthread -o "$tmp_dir/full-scan"
mkdir "$tmp_dir/full-scan-run"
(
  cd "$tmp_dir/full-scan-run"
  ZRAY_INST=1 ZRAY_LOGFILE="$tmp_dir/full-scan.zlog" \
    "$tmp_dir/full-scan" >stdout.log 2>stderr.log
)
if grep -q 'unbalanced region(s) at thread exit' \
  "$tmp_dir/full-scan-run/stderr.log"; then
  fail "full-scan exits left the runtime region depth unbalanced"
fi
pass "full-scan regions close on every function exit"

# A deliberately malformed instrumented program should produce a useful
# diagnostic rather than silently accepting a nonzero region depth.
"$llvm_link" "$input_dir/unbalanced-region.ll" "$runtime" \
  -S -o "$tmp_dir/unbalanced-region.linked.ll"
"$cxx" "$tmp_dir/unbalanced-region.linked.ll" -pthread \
  -o "$tmp_dir/unbalanced-region"
mkdir "$tmp_dir/unbalanced-run"
touch "$tmp_dir/empty.zlog"
(
  cd "$tmp_dir/unbalanced-run"
  ZRAY_LOGFILE="$tmp_dir/empty.zlog" \
    "$tmp_dir/unbalanced-region" >stdout.log 2>stderr.log
)
grep -q 'unbalanced region(s) at thread exit' \
  "$tmp_dir/unbalanced-run/stderr.log" || \
  fail "runtime did not report an unbalanced region"
grep -q 'region 0 depth=1' "$tmp_dir/unbalanced-run/stderr.log" || \
  fail "runtime diagnostic did not identify region 0 at depth 1"
pass "runtime reports unbalanced region depth"

printf 'All ZRay regression tests passed.\n'
