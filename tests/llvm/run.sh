#!/usr/bin/env bash
#
# Harbour LLVM IR backend — generate, validate, compare, and report.
#
# For every `tests/llvm/*.prg` program this script:
#   1. compiles it with the C backend  (harbour -> hbmk2)        -> reference exe
#   2. compiles it with the LLVM backend (harbour -GL)           -> .ll IR text
#   3. validates the .ll with the LLVM verifier (clang -x ir)
#   4. turns the .ll into an object and links it with hbmk2      -> LLVM exe
#   5. runs both executables and diffs their output
#   6. writes an HTML report to <output_dir>/index.html
#
# Exit status is non-zero if any program fails validation or the outputs differ,
# so this doubles as a CI gate.
#
# Usage:  tests/llvm/run.sh [output_dir]
# Env:    CLANG  - clang binary to use (default: clang)
#
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

OUT="${1:-build/llvm-ci}"
CLANG="${CLANG:-clang}"
rm -rf "$OUT"
mkdir -p "$OUT"

HB="$(find bin -type f \( -name harbour -o -name harbour.exe \) 2>/dev/null | head -1)"
HBMK2="$(find bin -type f \( -name hbmk2 -o -name hbmk2.exe \) 2>/dev/null | head -1)"

if [ -z "$HB" ] || [ -z "$HBMK2" ]; then
   echo "ERROR: harbour / hbmk2 not found under bin/ — build Harbour first."
   exit 1
fi
echo "harbour : $HB"
echo "hbmk2   : $HBMK2"
echo "clang   : $($CLANG --version 2>/dev/null | head -1)"
echo

fail=0
rows=""

for prg in tests/llvm/*.prg; do
   [ -e "$prg" ] || continue
   name="$(basename "$prg" .prg)"
   echo "=== $name ==="
   status_ir="ok"; status_run="ok"; note=""

   # --- 2. LLVM backend: emit IR text ---
   if ! "$HB" -GL -q -o"$OUT/${name}_ll" "$prg" 2> "$OUT/${name}.harbour.log"; then
      status_ir="FAIL"; note="harbour -GL failed"; fail=1
   fi

   # --- 3. validate the IR with the LLVM verifier ---
   if [ "$status_ir" = "ok" ]; then
      if ! "$CLANG" -S -emit-llvm -x ir "$OUT/${name}_ll.ll" -o /dev/null \
            2> "$OUT/${name}.verify.log"; then
         status_ir="FAIL"; note="LLVM verifier rejected the IR"; fail=1
      fi
   fi

   # --- 1. C backend reference build ---
   if [ "$status_ir" = "ok" ]; then
      if ! "$HBMK2" -q -o"$OUT/${name}_c" "$prg" 2> "$OUT/${name}.cbuild.log"; then
         status_run="FAIL"; note="C backend build failed"; fail=1
      fi
   fi

   # --- 4. compile IR to object, link with the Harbour runtime ---
   if [ "$status_ir" = "ok" ] && [ "$status_run" = "ok" ]; then
      if ! "$CLANG" -c -x ir "$OUT/${name}_ll.ll" -o "$OUT/${name}_ll.o" \
            2> "$OUT/${name}.llc.log"; then
         status_run="FAIL"; note="clang could not compile the IR"; fail=1
      elif ! "$HBMK2" -q -o"$OUT/${name}_ll" "$OUT/${name}_ll.o" \
            2> "$OUT/${name}.llink.log"; then
         status_run="FAIL"; note="linking the LLVM object failed"; fail=1
      fi
   fi

   # --- 5. run both and diff ---
   if [ "$status_ir" = "ok" ] && [ "$status_run" = "ok" ]; then
      "$OUT/${name}_c"  > "$OUT/${name}_c.out"  2>&1 || true
      "$OUT/${name}_ll" > "$OUT/${name}_ll.out" 2>&1 || true
      if ! diff -u "$OUT/${name}_c.out" "$OUT/${name}_ll.out" > "$OUT/${name}.diff"; then
         status_run="FAIL"; note="C and LLVM output differ"; fail=1
      fi
   fi

   echo "  IR validation : $status_ir"
   echo "  run / compare : $status_run"
   [ -n "$note" ] && echo "  note          : $note"
   echo

   # collect an HTML row
   src="$(sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g' "$prg")"
   ir="(not generated)"
   [ -f "$OUT/${name}_ll.ll" ] && \
      ir="$(sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g' "$OUT/${name}_ll.ll")"
   out="(not run)"
   [ -f "$OUT/${name}_ll.out" ] && \
      out="$(sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g' "$OUT/${name}_ll.out")"
   rows="${rows}
   <section>
     <h2>${name}.prg
       <span class=\"badge ${status_ir}\">IR ${status_ir}</span>
       <span class=\"badge ${status_run}\">run ${status_run}</span>
     </h2>
     ${note:+<p class=\"note\">${note}</p>}
     <h3>xBase source</h3><pre class=\"src\">${src}</pre>
     <h3>Generated LLVM IR</h3><pre class=\"ir\">${ir}</pre>
     <h3>Program output</h3><pre class=\"out\">${out}</pre>
   </section>"
done

# --- 6. HTML report ---
cat > "$OUT/index.html" <<HTML
<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<title>Harbour LLVM IR backend</title>
<style>
 body{font:15px/1.5 system-ui,sans-serif;max-width:1000px;margin:2rem auto;padding:0 1rem;color:#1a1a1a}
 h1{border-bottom:2px solid #ddd;padding-bottom:.3rem}
 pre{background:#f6f8fa;border:1px solid #e1e4e8;border-radius:6px;padding:.8rem;overflow:auto}
 pre.ir{background:#0d1117;color:#c9d1d9}
 .badge{font-size:.7rem;padding:.15rem .5rem;border-radius:1rem;color:#fff;vertical-align:middle}
 .badge.ok{background:#1a7f37}.badge.FAIL{background:#cf222e}
 .note{color:#cf222e;font-weight:600}
 footer{margin-top:3rem;color:#666;font-size:.85rem;border-top:1px solid #ddd;padding-top:1rem}
</style></head><body>
<h1>Harbour &rarr; LLVM IR backend</h1>
<p>Each xBase program below is compiled with <code>harbour -GL</code>, which emits
LLVM IR text instead of C. The IR is checked by the LLVM verifier, linked into a
native executable, and its output is compared against the standard C backend.</p>
<p>Generated $(date -u '+%Y-%m-%d %H:%M UTC').</p>
${rows}
<footer>Harbour LLVM backend project &mdash;
<a href="https://github.com/FiveTechSoft/harbourllvm">github.com/FiveTechSoft/harbourllvm</a></footer>
</body></html>
HTML

echo "report: $OUT/index.html"
if [ "$fail" -ne 0 ]; then
   echo "RESULT: FAILURES detected"
   exit 1
fi
echo "RESULT: all programs validated and matched the C backend"
