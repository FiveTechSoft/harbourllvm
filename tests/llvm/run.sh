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
# Resolve the clang binary.  Honor an explicit $CLANG env var if set.
# If CLANG is unset and bare 'clang' is not on PATH, fall back to amdclang++
# (first on PATH, then at the known LLVM install prefix on Windows).
if [ -z "${CLANG+x}" ]; then
   if command -v clang >/dev/null 2>&1; then
      CLANG="clang"
   elif command -v amdclang++ >/dev/null 2>&1; then
      CLANG="amdclang++"
   elif [ -x "/c/Program Files/LLVM/bin/amdclang++.exe" ]; then
      CLANG="/c/Program Files/LLVM/bin/amdclang++.exe"
   else
      CLANG="clang"   # last resort — will fail with a clear error below
   fi
fi
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
echo "clang   : $("$CLANG" --version 2>/dev/null | head -1)"
echo

# HTML-escape stdin
esc() { sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g'; }

# Run a test executable with a hard time limit and no controlling input, so a
# program that blocks (terminal GT on a non-tty, waiting for a key, ...) cannot
# hang the whole job. Falls back to a plain run if `timeout` is unavailable.
run_exe() {
   if command -v timeout >/dev/null 2>&1; then
      timeout 60 "$1" < /dev/null
   else
      "$1" < /dev/null
   fi
}

GIT_SHA="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
HB_VER="$("$HB" -build 2>/dev/null | head -1 || echo Harbour)"

total=0
passed=0
fail=0
cards=""

for prg in tests/llvm/*.prg; do
   [ -e "$prg" ] || continue
   name="$(basename "$prg" .prg)"
   total=$((total + 1))
   echo "=== $name ==="
   status_ir="ok"; status_run="ok"; note=""

   # --- 2. LLVM backend: emit IR text ---
   if ! "$HB" -GL -q -i"$ROOT/include" -o"$OUT/${name}_ll" "$prg" 2> "$OUT/${name}.harbour.log"; then
      status_ir="FAIL"; note="harbour -GL failed"
   fi

   # --- 3. validate the IR with the LLVM verifier ---
   if [ "$status_ir" = "ok" ]; then
      if ! "$CLANG" -S -emit-llvm -x ir "$OUT/${name}_ll.ll" -o /dev/null \
            2> "$OUT/${name}.verify.log"; then
         status_ir="FAIL"; note="LLVM verifier rejected the IR"
      fi
   fi

   # --- 3b. straight-line / fallback assertion ---
   # For non-fallback programs: the IR must use hb_vmsh_ shims and HB_FUN_MAIN
   # must contain no "call void @hb_vmExecute" (i.e. it was fully straight-lined).
   # For "fallback": HB_FUN_MAIN must still route through hb_vmExecute.
   #
   # We extract the HB_FUN_MAIN body with awk (from the define line to the
   # closing '}' at column 0) and grep within that excerpt.
   status_sl="ok"; sl_note=""
   if [ "$status_ir" = "ok" ] && [ -f "$OUT/${name}_ll.ll" ]; then
      # Extract the body of HB_FUN_MAIN (or HB_FUN_<NAME> for the primary fn)
      main_body="$(awk '/^define.*@HB_FUN_MAIN\(\)/{found=1} found{print} found && /^\}$/{exit}' \
                      "$OUT/${name}_ll.ll")"
      if [ "$name" = "fallback" ]; then
         # fallback.prg: expect hb_vmExecute call inside HB_FUN_MAIN
         if echo "$main_body" | grep -q "call void @hb_vmExecute"; then
            sl_note="fallback: hb_vmExecute call present in HB_FUN_MAIN (expected)"
         else
            status_sl="FAIL"
            sl_note="fallback: expected hb_vmExecute in HB_FUN_MAIN but not found"
         fi
      else
         # straight-line programs: must use hb_vmsh_ and must NOT call hb_vmExecute
         # in HB_FUN_MAIN (note: the 'declare' line is fine — we check 'call void').
         # Use grep -q (quiet, boolean) to avoid the "grep -c with || echo 0" trap:
         # grep -c exits 1 when count is 0, which would trigger || echo 0 and produce "0\n0".
         if grep -q "hb_vmsh_" "$OUT/${name}_ll.ll" 2>/dev/null; then
            has_vmsh=1
         else
            has_vmsh=0
         fi
         if echo "$main_body" | grep -q "call void @hb_vmExecute" 2>/dev/null; then
            has_fallback=1
         else
            has_fallback=0
         fi
         if [ "$has_vmsh" -gt 0 ] && [ "$has_fallback" -eq 0 ]; then
            sl_note="straight-line: hb_vmsh_ used, no hb_vmExecute in HB_FUN_MAIN"
         elif [ "$has_fallback" -gt 0 ]; then
            # Check if this program is required to straight-line.
            # Group A (loop/compound/forstep/arrays), B (arraylit/hashlit/arraydim),
            # C (memvar/dbfield), D (oop), E (foreach), F (switchstmt), G (codeblock), H (macro),
            # and I (sequence) corpus programs must NOT fall back — hard failure.
            case "$name" in
               loop|compound|forstep|compound2|forstep_var|\
               arraylit|hashlit|arraydim|\
               arraymdim|arrayref|\
               memvar|dbfield|\
               oop|oopclass|\
               foreach|switchstmt|codeblock|macro|sequence)
                  status_sl="FAIL"
                  sl_note="FAIL: HB_FUN_MAIN fell back to hb_vmExecute — expected straight-line for $name"
                  ;;
               *)
                  # Other programs with unsupported opcodes: acceptable fallback
                  sl_note="note: HB_FUN_MAIN fell back to hb_vmExecute (unsupported opcodes)"
                  ;;
            esac
         else
            status_sl="FAIL"
            sl_note="straight-line: neither hb_vmsh_ nor hb_vmExecute found in IR"
         fi
      fi
      echo "  straight-line : $status_sl${sl_note:+ — $sl_note}"
      # A broken fallback assertion is a hard failure
      if [ "$status_sl" = "FAIL" ]; then
         [ "$status_ir" = "ok" ] && status_ir="FAIL"
         note="$sl_note"
      fi
   fi

   # --- 1. C backend reference build (gtstd: non-interactive, CI-safe) ---
   if [ "$status_ir" = "ok" ]; then
      if ! "$HBMK2" -q -gtstd -i"$ROOT/include" -o"$OUT/${name}_c" "$prg" \
            2> "$OUT/${name}.cbuild.log"; then
         status_run="FAIL"; note="C backend build failed"
      fi
   fi

   # --- 4. compile IR to object, link with the Harbour runtime ---
   if [ "$status_ir" = "ok" ] && [ "$status_run" = "ok" ]; then
      if ! "$CLANG" -c -x ir "$OUT/${name}_ll.ll" -o "$OUT/${name}_ll.o" \
            2> "$OUT/${name}.llc.log"; then
         status_run="FAIL"; note="clang could not compile the IR"
      elif ! "$HBMK2" -q -gtstd -o"$OUT/${name}_ll" "$OUT/${name}_ll.o" \
            2> "$OUT/${name}.llink.log"; then
         status_run="FAIL"; note="linking the LLVM object failed"
      fi
   fi

   # --- 5. run both and diff ---
   diff_txt=""
   if [ "$status_ir" = "ok" ] && [ "$status_run" = "ok" ]; then
      run_exe "$OUT/${name}_c"  > "$OUT/${name}_c.out"  2>&1 || true
      run_exe "$OUT/${name}_ll" > "$OUT/${name}_ll.out" 2>&1 || true
      if ! diff -u "$OUT/${name}_c.out" "$OUT/${name}_ll.out" > "$OUT/${name}.diff"; then
         status_run="FAIL"; note="C and LLVM output differ"
         diff_txt="$(esc < "$OUT/${name}.diff")"
      fi
   fi

   ok="pass"
   if [ "$status_ir" != "ok" ] || [ "$status_run" != "ok" ]; then
      ok="fail"; fail=$((fail + 1))
   else
      passed=$((passed + 1))
   fi

   echo "  IR validation : $status_ir"
   echo "  run / compare : $status_run"
   [ -n "$note" ] && echo "  note          : $note"
   echo

   # gather the panes
   src="$(esc < "$prg")"
   ir="(not generated)"
   [ -f "$OUT/${name}_ll.ll" ] && ir="$(esc < "$OUT/${name}_ll.ll")"
   out="(not run)"
   [ -f "$OUT/${name}_ll.out" ] && out="$(esc < "$OUT/${name}_ll.out")"
   irlines="0"
   [ -f "$OUT/${name}_ll.ll" ] && irlines="$(wc -l < "$OUT/${name}_ll.ll" | tr -d ' ')"

   bcl_ir="bad"; [ "$status_ir" = "ok" ] && bcl_ir="good"
   bcl_run="bad"; [ "$status_run" = "ok" ] && bcl_run="good"
   bcl_sl="bad"
   sl_label="SL n/a"
   if [ -n "$sl_note" ]; then
      if echo "$sl_note" | grep -q "straight-line:"; then
         bcl_sl="good"; sl_label="SL ok"
      elif echo "$sl_note" | grep -q "fallback:.*expected"; then
         bcl_sl="good"; sl_label="fallback ok"
      elif echo "$sl_note" | grep -q "note:.*fell back"; then
         bcl_sl="warn"; sl_label="SL partial"
      else
         sl_label="SL FAIL"
      fi
   fi

   dlink=""
   [ -f "$OUT/${name}_ll.ll" ] && \
      dlink="<a class=\"dl\" href=\"${name}_ll.ll\" download>download .ll</a>"
   diffblock=""
   [ -n "$diff_txt" ] && \
      diffblock="<div class=\"pane diff\"><h3>Output diff (C vs LLVM)</h3><pre>${diff_txt}</pre></div>"
   slblock=""
   [ -n "$sl_note" ] && \
      slblock="<p class=\"slnote\">${sl_note}</p>"

   cards="${cards}
   <article class=\"test ${ok}\" id=\"${name}\">
     <header>
       <h2>${name}.prg</h2>
       <span class=\"badge ${bcl_ir}\">IR ${status_ir}</span>
       <span class=\"badge ${bcl_run}\">run ${status_run}</span>
       <span class=\"badge ${bcl_sl}\">${sl_label}</span>
     </header>
     ${slblock}
     ${note:+<p class=\"note\">${note}</p>}
     <div class=\"grid\">
       <div class=\"pane src\">
         <h3>xBase source</h3>
         <pre><code>${src}</code></pre>
       </div>
       <div class=\"pane ir\">
         <h3>LLVM IR <small>(${irlines} lines)</small> ${dlink}</h3>
         <pre><code class=\"language-llvm\">${ir}</code></pre>
       </div>
     </div>
     <div class=\"pane out\">
       <h3>Program output</h3>
       <pre>${out}</pre>
     </div>
     ${diffblock}
   </article>"
done

# --- summary banner ---
if [ "$fail" -eq 0 ]; then
   sum_class="good"
   sum_text="All ${total} programs validated and matched the C backend"
else
   sum_class="bad"
   sum_text="${fail} of ${total} programs failed"
fi

# --- 6. HTML report ---
cat > "$OUT/index.html" <<HTML
<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Harbour &rarr; LLVM IR</title>
<link rel="stylesheet"
 href="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/github-dark.min.css">
<style>
 :root{--bg:#0d1117;--card:#161b22;--line:#30363d;--fg:#e6edf3;--mut:#8b949e;
       --good:#2ea043;--bad:#da3633;--accent:#58a6ff}
 *{box-sizing:border-box}
 body{margin:0;background:var(--bg);color:var(--fg);
      font:15px/1.6 -apple-system,Segoe UI,Roboto,sans-serif}
 .wrap{max-width:1180px;margin:0 auto;padding:2rem 1.2rem 4rem}
 header.top h1{margin:0;font-size:1.7rem}
 header.top h1 .arrow{color:var(--accent)}
 .lead{color:var(--mut);margin:.4rem 0 1.4rem}
 .meta{color:var(--mut);font-size:.82rem;margin-bottom:1.4rem}
 .meta code{color:var(--fg)}
 .summary{display:flex;align-items:center;gap:.8rem;padding:.9rem 1.1rem;
          border-radius:10px;font-weight:600;margin-bottom:1.8rem;border:1px solid var(--line)}
 .summary.good{background:rgba(46,160,67,.12);border-color:var(--good)}
 .summary.bad{background:rgba(218,54,51,.12);border-color:var(--bad)}
 .dot{width:.7rem;height:.7rem;border-radius:50%}
 .summary.good .dot{background:var(--good)}.summary.bad .dot{background:var(--bad)}
 article.test{background:var(--card);border:1px solid var(--line);
              border-radius:12px;padding:1.1rem 1.2rem;margin-bottom:1.5rem}
 article.test.fail{border-color:var(--bad)}
 article.test header{display:flex;align-items:center;gap:.6rem;flex-wrap:wrap}
 article.test h2{margin:0;font-size:1.15rem;flex:1;font-family:ui-monospace,monospace}
 .badge{font-size:.7rem;font-weight:700;padding:.18rem .55rem;border-radius:1rem;
        text-transform:uppercase;letter-spacing:.03em}
 .badge.good{background:rgba(46,160,67,.2);color:#3fb950}
 .badge.bad{background:rgba(218,54,51,.2);color:#f85149}
 .badge.warn{background:rgba(210,153,34,.2);color:#d4a017}
 .note{color:#f85149;font-weight:600;margin:.6rem 0 0}
 .slnote{color:#8b949e;font-size:.85rem;margin:.4rem 0 0;font-style:italic}
 .grid{display:grid;grid-template-columns:5fr 7fr;gap:1rem;margin-top:.9rem}
 @media(max-width:760px){.grid{grid-template-columns:1fr}}
 .pane h3{font-size:.78rem;text-transform:uppercase;letter-spacing:.05em;
          color:var(--mut);margin:.2rem 0 .4rem;display:flex;gap:.6rem;align-items:baseline}
 .pane h3 small{text-transform:none;letter-spacing:0}
 .pane pre{margin:0;background:#010409;border:1px solid var(--line);border-radius:8px;
           padding:.7rem .85rem;overflow:auto;max-height:430px;font-size:.82rem;
           font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
 .pane.out pre,.pane.diff pre{max-height:240px;margin-top:.9rem}
 .pane.diff pre{color:#f0883e}
 a.dl{font-size:.72rem;color:var(--accent);text-decoration:none;
      border:1px solid var(--line);padding:.1rem .5rem;border-radius:1rem}
 a.dl:hover{border-color:var(--accent)}
 footer{color:var(--mut);font-size:.82rem;border-top:1px solid var(--line);
        padding-top:1.2rem;margin-top:2.5rem}
 footer a{color:var(--accent)}
</style></head><body><div class="wrap">

<header class="top">
 <h1>Harbour <span class="arrow">&rarr;</span> LLVM IR</h1>
 <p class="lead">Each xBase program is compiled with <code>harbour -GL</code>,
 which emits LLVM IR instead of C. The IR is checked by the LLVM verifier,
 linked into a native executable, and its output is compared against the
 standard C backend.</p>
</header>

<p class="meta">commit <code>${GIT_SHA}</code> &middot; ${HB_VER} &middot;
 generated $(date -u '+%Y-%m-%d %H:%M UTC')</p>

<div class="summary ${sum_class}"><span class="dot"></span>${sum_text}</div>

${cards}

<footer>
 Harbour LLVM backend &mdash;
 <a href="https://github.com/FiveTechSoft/harbourllvm">github.com/FiveTechSoft/harbourllvm</a><br>
 Plan 1: IR text emitter. Plan 2: embeds libLLVM + lld. Plan 3: straight-line IR,
 removes the interpreter dispatch loop. See <code>docs/superpowers/</code>.
</footer>

</div>
<script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/languages/llvm.min.js"></script>
<script>hljs.highlightAll();</script>
</body></html>
HTML

echo "report: $OUT/index.html"
if [ "$fail" -ne 0 ]; then
   echo "RESULT: FAILURES detected"
   exit 1
fi
echo "RESULT: all programs validated and matched the C backend"
