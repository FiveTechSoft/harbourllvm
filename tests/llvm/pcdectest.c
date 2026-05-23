/*
 * pcdectest.c — Unit test for the pcode opcode decoder (Tasks 2 & 3, Plan 3).
 *
 * Walks a hand-built pcode buffer and checks instruction boundaries (Task 2),
 * then tests the basic-block / jump-target analysis (Task 3).
 *
 * Build:
 *   gcc tests/llvm/pcdectest.c src/compiler/hb_pcdec.c \
 *       -I include -I src/compiler -L lib/win/mingw64 -lhbcommon \
 *       -o build/pcdectest.exe
 * Run:
 *   ./build/pcdectest.exe
 * Expected:
 *   pcdec: 4 instructions, all boundaries correct
 *   pcdec: jump analysis correct
 *   pcdec: HB_P_SWITCH length correct
 *   pcdec: HB_P_PUSHBLOCK* supported
 *   pcdec: 14 macro opcodes supported, MPUSH* family still unsupported
 */

#include "hb_pcdec.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Minimal stubs for hb_xgrab / hb_xfree so this test compiles standalone
 * without linking the full Harbour VM library. */
void * hb_xgrab( HB_SIZE nSize )         { return malloc( ( size_t ) nSize ); }
void   hb_xfree( void * pMem )           { free( pMem ); }

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

   {
      /* HB_P_JUMP with 2-byte displacement +3 (little-endian): jumps to its own
       * fall-through (offset 3).  Buffer: [JUMP, 3, 0, ENDPROC].
       * Displacement = HB_PCODE_MKSHORT({3,0}) = 3.  target = 0 + 3 = 3.
       * Expected leaders: offset 0 (entry) and offset 3 (jump target). */
      HB_BYTE jbuf[] = { HB_P_JUMP, 3, 0, HB_P_ENDPROC };
      HB_PCMAP map;
      HB_BOOL  ok = hb_pcodeAnalyze( jbuf, sizeof( jbuf ), &map );
      assert( ok );
      assert( map.abLeader[ 0 ] == 1 );   /* entry           */
      assert( map.abLeader[ 3 ] == 1 );   /* jump target     */
      hb_xfree( map.abLeader );
      printf( "pcdec: jump analysis correct\n" );
   }

   {
      /* HB_P_SWITCH, count=1, [ PUSHLONG 7 , JUMP +0 ] -> total 11 bytes */
      HB_BYTE sw[] = {
         HB_P_SWITCH, 1, 0,
         HB_P_PUSHLONG, 7, 0, 0, 0,
         HB_P_JUMP, 0, 0
      };
      HB_SIZE len = hb_pcodeInstrLen( sw );
      assert( len == 11 );
      printf( "pcdec: HB_P_SWITCH length correct\n" );
   }

   {
      /* Group G: the three PUSHBLOCK* opcodes are now in the straight-line
       * subset, and a PUSHBLOCKSHORT's length is its 1-byte size operand. */
      HB_BYTE blk[]  = { HB_P_PUSHBLOCKSHORT, 4, HB_P_PUSHNIL, HB_P_ENDBLOCK };
      HB_BYTE blk2[] = { HB_P_PUSHBLOCK, 5, 0, HB_P_PUSHNIL, HB_P_ENDBLOCK };
      HB_BYTE blk3[] = { HB_P_PUSHBLOCKLARGE, 5, 0, 0, HB_P_ENDBLOCK };
      assert( hb_pcodeInstrLen( blk ) == 4 );
      assert( hb_pcodeInstrLen( blk2 ) == 5 );   /* 2-byte MKUSHORT size operand */
      assert( hb_pcodeInstrLen( blk3 ) == 5 );   /* 3-byte MKUINT24 size operand */
      assert( hb_pcInfo[ HB_P_PUSHBLOCK      ].fSupported );
      assert( hb_pcInfo[ HB_P_PUSHBLOCKSHORT ].fSupported );
      assert( hb_pcInfo[ HB_P_PUSHBLOCKLARGE ].fSupported );
      printf( "pcdec: HB_P_PUSHBLOCK* supported\n" );
   }

   {
      /* Group H: the 14 compiler-emitted macro opcodes are now in the
       * straight-line subset. MPUSHBLOCK / MPUSHSTR family stays HB_FALSE. */
      assert( hb_pcInfo[ HB_P_MACROPOP         ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROPOPALIASED  ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROPUSH        ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROARRAYGEN    ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROPUSHLIST    ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROPUSHINDEX   ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROPUSHPARE    ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROPUSHALIASED ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROSYMBOL      ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROTEXT        ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROFUNC        ].fSupported );
      assert( hb_pcInfo[ HB_P_MACRODO          ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROPUSHREF     ].fSupported );
      assert( hb_pcInfo[ HB_P_MACROSEND        ].fSupported );
      /* MPUSH* family must stay unsupported. */
      assert( ! hb_pcInfo[ HB_P_MPUSHBLOCK      ].fSupported );
      assert( ! hb_pcInfo[ HB_P_MPUSHSTR        ].fSupported );
      assert( ! hb_pcInfo[ HB_P_MPUSHBLOCKLARGE ].fSupported );
      assert( ! hb_pcInfo[ HB_P_MPUSHSTRLARGE   ].fSupported );
      printf( "pcdec: 14 macro opcodes supported, MPUSH* family still unsupported\n" );
   }

   return 0;
}
