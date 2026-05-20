/*
 * pcdectest.c — Unit test for the pcode opcode decoder (Task 2, Plan 3).
 *
 * Walks a hand-built pcode buffer and checks instruction boundaries.
 *
 * Build:
 *   gcc tests/llvm/pcdectest.c src/compiler/hb_pcdec.c \
 *       -I include -I src/compiler -o build/pcdectest.exe
 * Run:
 *   ./build/pcdectest.exe
 * Expected:
 *   pcdec: 4 instructions, all boundaries correct
 */

#include "hb_pcdec.h"
#include <stdio.h>
#include <assert.h>

int main( void )
{
   /* HB_P_PUSHINT (3 bytes) HB_P_PUSHINT (3) HB_P_PLUS (1) HB_P_ENDPROC (1)
    * Total: 8 bytes, 4 instructions.
    * HB_P_PUSHINT = 93 = 0x5D; operand is a 2-byte signed short (little-endian).
    * HB_P_PLUS    = 72 = 0x48
    * HB_P_ENDPROC =  7 = 0x07
    */
   HB_BYTE buf[] = {
      HB_P_PUSHINT, 2, 0,    /* push integer 2  (3 bytes) */
      HB_P_PUSHINT, 3, 0,    /* push integer 3  (3 bytes) */
      HB_P_PLUS,             /* +                (1 byte)  */
      HB_P_ENDPROC           /* end              (1 byte)  */
   };
   HB_SIZE pos = 0;
   int     n   = 0;

   while( pos < sizeof( buf ) )
   {
      HB_SIZE len = hb_pcodeInstrLen( &buf[ pos ] );
      assert( len > 0 );
      pos += len;
      ++n;
   }
   assert( pos == sizeof( buf ) );
   assert( n == 4 );
   printf( "pcdec: %d instructions, all boundaries correct\n", n );
   return 0;
}
