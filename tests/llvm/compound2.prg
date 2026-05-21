/* compound2.prg — exercises compound-assignment opcodes not covered by compound.prg
 *
 * Shims hit by this program (new to the corpus):
 *   hb_vmsh_modeqpop    — n %= m  (statement form, variable RHS)
 *   hb_vmsh_expeqpop    — n ^= m  (statement form, variable RHS)
 *   hb_vmsh_minuseqpop  — n -= m  (statement form, variable RHS; optimizer
 *                                  keeps MINUSEQPOP only when RHS is non-constant)
 *   hb_vmsh_multeq      — ? (n *= m) (expr form, leaves result on stack)
 *   hb_vmsh_diveq       — ? (n /= m) (expr form)
 *   hb_vmsh_modeq       — ? (n %= m) (expr form)
 *   hb_vmsh_expeq       — ? (n ^= m) (expr form)
 *   hb_vmsh_pluseq      — ? (n += m) (expr form, variable RHS; constant RHS
 *                                  is optimised to localnearaddint + pushlocal)
 *   hb_vmsh_minuseq     — ? (n -= m) (expr form, variable RHS)
 */
function Main()
   local n := 64
   local m := 4

   /* --- statement forms (POP variants) --- */
   n -= m          /* minuseqpop  (variable RHS; constant would become addint) */
   n %= m          /* modeqpop                                                  */
   n := 64         /* reset                                                     */
   n ^= m          /* expeqpop  (n=64, m=4  →  64^4 = 16777216)               */

   /* --- expression forms (non-POP, result stays on stack) --- */
   n := 64
   ? ( n += m )    /* pluseq   — variable RHS keeps the non-POP form           */
   ? ( n -= m )    /* minuseq  — variable RHS                                  */
   ? ( n *= m )    /* multeq                                                    */
   ? ( n /= m )    /* diveq                                                     */
   n := 65
   ? ( n %= m )    /* modeq    (65 % 4 = 1)                                    */
   n := 2
   ? ( n ^= m )    /* expeq    (2^4 = 16)                                      */

   return nil
