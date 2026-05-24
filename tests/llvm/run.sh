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
<title>Harbour &rarr; LLVM &mdash; xBase compiled to native code, no external C toolchain</title>
<meta name="description" content="Harbour LLVM backend: harbour -GL compiles xBase .prg source directly to LLVM IR and links a native executable in-process. Continuously verified against the C backend.">
<link rel="stylesheet"
 href="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/github-dark.min.css">
<style>
 :root{--bg:#0d1117;--card:#161b22;--card2:#1c2128;--line:#30363d;--fg:#e6edf3;
       --mut:#8b949e;--good:#2ea043;--bad:#da3633;--accent:#58a6ff;
       --accent2:#a371f7;--warn:#d29922}
 *{box-sizing:border-box}
 html{scroll-behavior:smooth}
 body{margin:0;background:var(--bg);color:var(--fg);
      font:15.5px/1.65 -apple-system,Segoe UI,Roboto,sans-serif}
 .wrap{max-width:1180px;margin:0 auto;padding:0 1.2rem 4rem}

 /* HERO */
 .hero{padding:4rem 0 2rem;text-align:center;
       background:radial-gradient(ellipse 800px 400px at 50% 0%,rgba(88,166,255,.12),transparent 70%)}
 .hero .eyebrow{display:inline-block;font-size:.72rem;font-weight:700;
        letter-spacing:.18em;text-transform:uppercase;color:var(--accent);
        padding:.3rem .8rem;border:1px solid rgba(88,166,255,.4);border-radius:1rem;
        background:rgba(88,166,255,.06);margin-bottom:1.4rem}
 .hero h1{margin:0 0 1rem;font-size:clamp(2rem,5vw,3.2rem);font-weight:800;
       line-height:1.1;letter-spacing:-.02em}
 .hero h1 .arrow{background:linear-gradient(135deg,var(--accent),var(--accent2));
       -webkit-background-clip:text;background-clip:text;color:transparent}
 .hero .tagline{font-size:1.15rem;color:var(--mut);max-width:42rem;margin:0 auto 1.6rem}
 .hero .tagline strong{color:var(--fg)}
 .hero-actions{display:flex;justify-content:center;gap:.7rem;flex-wrap:wrap;margin-bottom:2rem}
 .btn{display:inline-block;padding:.6rem 1.1rem;border-radius:8px;font-weight:600;
       font-size:.92rem;text-decoration:none;border:1px solid var(--line);
       color:var(--fg);background:var(--card);transition:all .15s}
 .btn:hover{border-color:var(--accent);background:var(--card2)}
 .btn.primary{background:var(--accent);color:#0d1117;border-color:var(--accent)}
 .btn.primary:hover{background:#79b8ff;border-color:#79b8ff}

 /* STATUS PILL */
 .status-pill{display:inline-flex;align-items:center;gap:.6rem;padding:.55rem 1rem;
       border-radius:2rem;font-weight:600;font-size:.92rem;
       border:1px solid var(--line);margin-bottom:1.6rem}
 .status-pill.good{background:rgba(46,160,67,.12);border-color:var(--good);color:#3fb950}
 .status-pill.bad{background:rgba(218,54,51,.12);border-color:var(--bad);color:#f85149}
 .status-pill .dot{width:.55rem;height:.55rem;border-radius:50%}
 .status-pill.good .dot{background:var(--good);box-shadow:0 0 0 4px rgba(46,160,67,.25)}
 .status-pill.bad .dot{background:var(--bad);box-shadow:0 0 0 4px rgba(218,54,51,.25)}

 /* STATS GRID */
 .stats{display:grid;grid-template-columns:repeat(4,1fr);gap:1rem;
       max-width:46rem;margin:0 auto 1.6rem}
 @media(max-width:560px){.stats{grid-template-columns:repeat(2,1fr)}}
 .stat{background:var(--card);border:1px solid var(--line);border-radius:10px;
       padding:1rem .8rem;text-align:center}
 .stat .num{font-size:1.9rem;font-weight:800;color:var(--accent);line-height:1.1}
 .stat .lbl{font-size:.7rem;text-transform:uppercase;letter-spacing:.08em;
       color:var(--mut);margin-top:.3rem}

 /* META */
 .meta{color:var(--mut);font-size:.8rem;text-align:center;margin:1rem 0 2rem}
 .meta code{color:var(--fg);background:var(--card);padding:.1rem .35rem;
       border-radius:4px;font-size:.85rem}

 /* SECTION */
 section{margin:3.5rem 0}
 section h2{font-size:1.6rem;margin:0 0 .4rem;letter-spacing:-.01em}
 section .lead{color:var(--mut);margin:0 0 1.6rem;font-size:1rem}

 /* TWO-COL FEATURES */
 .features{display:grid;grid-template-columns:1fr 1fr;gap:1.4rem}
 @media(max-width:760px){.features{grid-template-columns:1fr}}
 .features .col{background:var(--card);border:1px solid var(--line);
       border-radius:12px;padding:1.4rem 1.5rem}
 .features h3{margin:0 0 .8rem;font-size:1.05rem;
       color:var(--accent);display:flex;align-items:center;gap:.5rem}
 .features ul{margin:0;padding-left:1.1rem;color:var(--fg)}
 .features li{margin:.4rem 0}
 .features li code{background:var(--card2);padding:.05rem .35rem;border-radius:4px;
       font-size:.86rem}

 /* TRY IT */
 .try-steps{display:grid;grid-template-columns:1fr;gap:1rem}
 .try-step{background:var(--card);border:1px solid var(--line);border-radius:12px;
       padding:1.2rem 1.4rem;display:grid;grid-template-columns:auto 1fr;
       gap:1rem 1.2rem;align-items:start}
 @media(max-width:560px){.try-step{grid-template-columns:1fr}}
 .try-step .num{font-size:1.6rem;font-weight:800;color:var(--accent);line-height:1;
       background:rgba(88,166,255,.1);border:1px solid rgba(88,166,255,.3);
       border-radius:8px;width:2.4rem;height:2.4rem;display:flex;
       align-items:center;justify-content:center}
 .try-step h3{margin:0 0 .3rem;font-size:1.05rem}
 .try-step p{margin:0 0 .6rem;color:var(--mut);font-size:.92rem}
 .try-step pre{margin:.4rem 0 0;background:#010409;border:1px solid var(--line);
       border-radius:8px;padding:.7rem .9rem;overflow:auto;font-size:.85rem;
       font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
 .try-step code.inl{background:var(--card2);padding:.05rem .35rem;border-radius:4px;
       font-size:.88rem}
 .try-step .stepbody > :first-child{margin-top:0}

 /* COVERAGE TABLE */
 .coverage{width:100%;border-collapse:collapse;background:var(--card);
       border:1px solid var(--line);border-radius:10px;overflow:hidden;font-size:.92rem}
 .coverage th,.coverage td{padding:.65rem .9rem;text-align:left;
       border-bottom:1px solid var(--line)}
 .coverage th{background:var(--card2);font-size:.78rem;text-transform:uppercase;
       letter-spacing:.06em;color:var(--mut);font-weight:600}
 .coverage tr:last-child td{border-bottom:0}
 .coverage td.tag{font-family:ui-monospace,monospace;font-weight:700;
       color:var(--accent);width:5rem}
 .coverage td.state{width:6rem;text-align:right;color:#3fb950;font-weight:600;font-size:.85rem}

 /* TEST CARDS — kept from original */
 article.test{background:var(--card);border:1px solid var(--line);
              border-radius:12px;padding:1.1rem 1.2rem;margin-bottom:1.5rem}
 article.test.fail{border-color:var(--bad)}
 article.test header{display:flex;align-items:center;gap:.6rem;flex-wrap:wrap}
 article.test h2{margin:0;font-size:1.05rem;flex:1;font-family:ui-monospace,monospace;
       letter-spacing:0}
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

 /* FOOTER */
 footer.site{color:var(--mut);font-size:.88rem;border-top:1px solid var(--line);
        padding:2rem 0 0;margin-top:3.5rem;display:grid;
        grid-template-columns:2fr 1fr 1fr;gap:2rem}
 @media(max-width:760px){footer.site{grid-template-columns:1fr}}
 footer.site h4{margin:0 0 .5rem;color:var(--fg);font-size:.82rem;
        text-transform:uppercase;letter-spacing:.06em}
 footer.site a{color:var(--accent);text-decoration:none}
 footer.site a:hover{text-decoration:underline}
 footer.site ul{list-style:none;padding:0;margin:0}
 footer.site li{margin:.25rem 0}
</style></head><body><div class="wrap">

<!-- HERO -->
<div class="hero">
 <span class="eyebrow">Harbour LLVM Backend &middot; v3.2.0dev</span>
 <h1>Harbour <span class="arrow">&rarr;</span> LLVM IR</h1>
 <p class="tagline">An <strong>xBase compiler</strong> that emits LLVM IR
  directly and links a <strong>native executable in-process</strong> &mdash;
  no external C/C++ toolchain on PATH, no second-stage build.</p>

 <div class="status-pill ${sum_class}"><span class="dot"></span>${sum_text}</div>

 <div class="stats">
  <div class="stat"><div class="num">9</div><div class="lbl">Opcode groups</div></div>
  <div class="stat"><div class="num">${total}</div><div class="lbl">Corpus programs</div></div>
  <div class="stat"><div class="num">${passed}</div><div class="lbl">Verified vs C</div></div>
  <div class="stat"><div class="num">0</div><div class="lbl">External deps</div></div>
 </div>

 <div class="hero-actions">
  <a class="btn primary" href="#try">Try it locally</a>
  <a class="btn" href="https://github.com/FiveTechSoft/harbourllvm">GitHub repository</a>
  <a class="btn" href="#tests">See test results</a>
 </div>

 <p class="meta">commit <code>${GIT_SHA}</code> &middot; ${HB_VER} &middot;
  generated $(date -u '+%Y-%m-%d %H:%M UTC')</p>
</div>

<!-- WHAT IT DOES + WHAT YOU GET -->
<section id="what">
 <h2>What it does, what you get</h2>
 <p class="lead">A fork of <a href="https://github.com/harbour/core">Harbour</a>
  &mdash; the free, multi-platform Clipper/xBase compiler &mdash; with an LLVM
  backend that turns <code>harbour</code> into a frontend, the way <code>clang</code>
  is a frontend for Objective-C.</p>

 <div class="features">
  <div class="col">
   <h3>How it works</h3>
   <ul>
    <li><code>harbour -GL hello.prg</code> parses the xBase source and emits
        <strong>LLVM IR text</strong> (.ll), not C.</li>
    <li>The embedded <strong>libLLVM C API</strong> turns that IR into a
        native object; the embedded <strong>LLD linker</strong> links the
        executable &mdash; all in-process, no second pass.</li>
    <li>Each pcode opcode in the supported subset becomes one
        <code>hb_vmsh_*</code> shim call: <strong>one basic block per opcode,
        no interpreter dispatch</strong> &mdash; straight-line native code.</li>
    <li>Functions using opcodes outside the subset fall back, whole-function,
        to the <code>hb_vmExecute</code> interpreter &mdash; correctness is
        always preserved.</li>
   </ul>
  </div>
  <div class="col">
   <h3>What you achieve</h3>
   <ul>
    <li><strong>Zero external toolchain.</strong> No gcc / MSVC / clang on
        PATH. No second-stage make. One <code>harbour.exe</code>.</li>
    <li><strong>100% Harbour-compatible.</strong> The shims call the same
        <code>hb_vm*</code> helpers the interpreter calls; every test
        diff-checks byte-for-byte against the C backend.</li>
    <li><strong>Nine opcode groups straight-lined</strong> &mdash; FOR loops,
        arrays, RDD/memvars, OOP, FOR EACH, SWITCH, codeblocks, macros,
        SEQUENCE (try/catch + try/finally).</li>
    <li><strong>Permanent fallback safety net.</strong> Programs that touch
        the (small) set of intentionally-unsupported opcodes still build and
        run correctly via the interpreter.</li>
    <li><strong>Open to future type specialization</strong> &mdash; removing
        dispatch overhead is the first half; specializing local int/double
        slots is the next bigger win.</li>
   </ul>
  </div>
 </div>
</section>

<!-- COVERAGE -->
<section id="coverage">
 <h2>Opcode coverage</h2>
 <p class="lead">All nine groups in the original decomposition complete. Each
  has its own spec + plan in
  <a href="https://github.com/FiveTechSoft/harbourllvm/tree/master/docs/superpowers">docs/superpowers/</a>.</p>
 <table class="coverage">
  <thead><tr><th>Group</th><th>Covers</th><th></th></tr></thead>
  <tbody>
   <tr><td class="tag">A</td><td>FOR loops + compound assignment (<code>+=</code>, <code>++</code>, <code>--</code>, &hellip;)</td><td class="state">done</td></tr>
   <tr><td class="tag">B</td><td>Arrays + hashes: literals, element access, <code>Array()</code>, <code>LOCAL a[m,n]</code></td><td class="state">done</td></tr>
   <tr><td class="tag">C</td><td>RDD fields, memvars, aliased access, workarea selection</td><td class="state">done</td></tr>
   <tr><td class="tag">D</td><td>OOP message sends, <code>Self</code>, <code>WITH OBJECT</code></td><td class="state">done</td></tr>
   <tr><td class="tag">E</td><td><code>FOR EACH</code> loops (and <code>DESCEND</code>) over arrays, hashes, strings</td><td class="state">done</td></tr>
   <tr><td class="tag">F</td><td><code>SWITCH</code> &mdash; lowered to a native LLVM <code>switch</code> instruction</td><td class="state">done</td></tr>
   <tr><td class="tag">G</td><td>Codeblock literals <code>{|args| ... }</code></td><td class="state">done</td></tr>
   <tr><td class="tag">H</td><td>Macros: <code>&amp;var</code>, <code>&amp;("expr")</code>, <code>text&hellip;endtext</code>, <code>@&amp;var</code></td><td class="state">done</td></tr>
   <tr><td class="tag">I</td><td><code>BEGIN SEQUENCE&hellip;RECOVER&hellip;END</code> (try/catch) and <code>&hellip;ALWAYS&hellip;END</code> (try/finally)</td><td class="state">done</td></tr>
  </tbody>
 </table>
</section>

<!-- TRY IT -->
<section id="try">
 <h2>Try it locally</h2>
 <p class="lead">From a clean clone to a running hello-world in four short
  steps. Linux/macOS and Windows (MinGW) both work.</p>

 <div class="try-steps">

  <div class="try-step">
   <div class="num">1</div>
   <div class="stepbody">
    <h3>Prerequisites</h3>
    <p>A standard C toolchain to build Harbour itself, plus an LLVM install
     for the test-suite verifier:</p>
    <pre><code># Linux / macOS
sudo apt install build-essential clang     # or: brew install llvm

# Windows (MinGW-w64 from winlibs.com)
# Add gcc + win-make.exe to PATH; install LLVM for Windows</code></pre>
   </div>
  </div>

  <div class="try-step">
   <div class="num">2</div>
   <div class="stepbody">
    <h3>Clone and build</h3>
    <p>Builds the compiler with the LLVM backend embedded:</p>
    <pre><code>git clone https://github.com/FiveTechSoft/harbourllvm
cd harbourllvm/core

# Linux / macOS
make

# Windows (MinGW)
set HB_COMPILER=mingw64
win-make.exe</code></pre>
   </div>
  </div>

  <div class="try-step">
   <div class="num">3</div>
   <div class="stepbody">
    <h3>Hello world &mdash; compile and run</h3>
    <p>Save this as <code class="inl">hello.prg</code>:</p>
    <pre><code>function Main()
   local cName := "world"
   ? "hello, " + cName
   return nil</code></pre>
    <p>Compile it with the LLVM backend (note <code class="inl">-GL</code>)
     and run the resulting executable:</p>
    <pre><code>./bin/&lt;platform&gt;/harbour -GL hello.prg
./hello
# hello, world</code></pre>
    <p>Inspect the intermediate IR if you want &mdash; it sits next to the
     binary as <code class="inl">hello.ll</code>.</p>
   </div>
  </div>

  <div class="try-step">
   <div class="num">4</div>
   <div class="stepbody">
    <h3>Run the corpus &mdash; diff against the C backend</h3>
    <p>The corpus compiles every <code class="inl">tests/llvm/*.prg</code>
     with both backends, runs the LLVM verifier on the emitted IR, links a
     native executable, and diffs its output against the standard C backend.
     <strong>Any difference fails the run.</strong></p>
    <pre><code>CLANG=clang tests/llvm/run.sh
# -> writes build/llvm-ci/index.html
# -> exits non-zero on any difference</code></pre>
    <p>Open <code class="inl">build/llvm-ci/index.html</code> &mdash; it is a
     page just like this one, with every program&rsquo;s source, generated IR,
     output, and pass/fail status. The
     <a href=".github/workflows/llvm-ir.yml">GitHub Actions workflow</a> runs
     this on every push and publishes the result to GitHub Pages.</p>
   </div>
  </div>

 </div>
</section>

<!-- TESTS -->
<section id="tests">
 <h2>Continuous verification</h2>
 <p class="lead">Below is the latest run. Every card is one
  <code>tests/llvm/*.prg</code> program &mdash; xBase source on the left,
  full generated LLVM IR on the right, output and diff at the bottom.
  Click <em>download .ll</em> to grab the IR.</p>

 ${cards}
</section>

<footer class="site">
 <div>
  <h4>Harbour LLVM backend</h4>
  <p>A fork of <a href="https://github.com/harbour/core">harbour/core</a> with
   an in-process LLVM frontend. Plans 1-3 + opcode groups A&ndash;I.
   Programs using opcodes outside the A&ndash;I subset (timestamp / date
   literals, <code>$</code> substring, static frames, var-arg frames,
   hidden strings, &hellip;) fall back, whole-function, to
   <code>hb_vmExecute</code> &mdash; a permanent, intentional safety net.
   Same license as Harbour (GPL + Harbour exception).</p>
 </div>
 <div>
  <h4>Project</h4>
  <ul>
   <li><a href="https://github.com/FiveTechSoft/harbourllvm">GitHub repository</a></li>
   <li><a href="https://github.com/FiveTechSoft/harbourllvm/tree/master/docs/superpowers">Specs &amp; plans</a></li>
   <li><a href="https://github.com/FiveTechSoft/harbourllvm/blob/master/README.md">README</a></li>
  </ul>
 </div>
 <div>
  <h4>References</h4>
  <ul>
   <li><a href="https://harbour.github.io/">Harbour project</a></li>
   <li><a href="https://llvm.org/">LLVM</a></li>
   <li><a href="https://en.wikipedia.org/wiki/XBase">xBase / Clipper</a></li>
  </ul>
 </div>
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
