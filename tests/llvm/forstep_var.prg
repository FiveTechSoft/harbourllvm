/* forstep_var.prg — FOR loop with a variable (runtime) step value
 *
 * Shims hit by this program (new to the corpus):
 *   hb_vmsh_fortest     — emitted by FOR loops whose step is not a compile-time
 *                         constant; fortest tests the sign of the step value to
 *                         decide whether the loop guard is < or >
 *   hb_vmsh_pluseq      — the loop counter increment uses PUSHLOCALREF + step +
 *                         PLUSEQ (non-POP form) when step is a variable
 *
 * (pushunref is already exercised by loop.prg and forstep.prg)
 */
function Main()
   local i, nSum := 0
   local nStep := 2

   /* step is a variable — compiler emits HB_P_FORTEST to handle either sign */
   for i := 1 to 10 step nStep
      nSum += i
   next
   ? nSum          /* 1+3+5+7+9 = 25 */

   /* negative variable step */
   nStep := -3
   for i := 10 to 1 step nStep
      nSum += i
   next
   ? nSum          /* 25 + (10+7+4+1) = 25+22 = 47 */

   return nil
