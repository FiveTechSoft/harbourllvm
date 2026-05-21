/*
 * LLVM IR text generation for the Harbour compiler.
 *
 * Copyright 2026 Harbour contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file LICENSE.txt.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA (or visit https://www.gnu.org/licenses/).
 *
 * As a special exception, the Harbour Project gives permission for
 * additional uses of the text contained in its release of Harbour.
 *
 * The exception is that, if you link the Harbour libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Harbour library code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public License.
 *
 * This exception applies only to the code released by the Harbour
 * Project under the name Harbour.  If you copy code from other
 * Harbour Project or Free Software Foundation releases into a copy of
 * Harbour, as the General Public License permits, this does not apply
 * to the code that you copied from other releases. If you copy and
 * integrate source code from other releases into your copy of Harbour,
 * subject to the GNU General Public License, your copy must comply with
 * the GNU General Public License.
 *
 * If you wish, you may redistribute it under the terms of the GNU
 * General Public License without exception.
 */

/*
 * Emits LLVM IR text (*.ll) directly via fprintf() — no libLLVM dependency.
 *
 * Architecture mirror of genc.c:  per-function pcode byte arrays +
 * hb_vmExecute() calls, symbol table registered via @llvm.global_ctors.
 *
 * Plan 1 scope: x86_64 only — HB_SYMBOLSCOPE stored as i64.
 */

#define _HB_API_INTERNAL_
#include "hbcomp.h"
#include "hb_llvmobj.h"
#include "hb_pcdec.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * Helper: emit one pcode byte as an LLVM IR string escape (\HH).
 * ------------------------------------------------------------------------- */
static void hb_llvmEmitByte( FILE * yyc, HB_BYTE b )
{
   fprintf( yyc, "\\%02X", ( unsigned ) b );
}

/* -------------------------------------------------------------------------
 * Helper: write "HB_FUN_<name>" to yyc, encoding non-identifier characters
 * as "x<HH>" exactly as hb_compGenCFunc() does in genc.c:463-497.
 * ------------------------------------------------------------------------- */
static void hb_llvmEmitFuncName( FILE * yyc, const char * szName )
{
   const char * p;

   fprintf( yyc, "HB_FUN_" );
   for( p = szName; *p; ++p )
   {
      unsigned char c = ( unsigned char ) *p;
      if( ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) ||
          ( c >= '0' && c <= '9' ) || c == '_' )
         fputc( c, yyc );
      else
         fprintf( yyc, "x%02x", c );
   }
}

/* -------------------------------------------------------------------------
 * Helper: compute the numeric scope flags for a symbol, mirroring what
 * genc.c prints symbolically as HB_FS_* names.
 *
 * HB_SYMBOLSCOPE values (from hbvmpub.h):
 *   HB_FS_PUBLIC   0x0001   HB_FS_STATIC  0x0002   HB_FS_FIRST  0x0004
 *   HB_FS_INIT     0x0008   HB_FS_EXIT    0x0010   HB_FS_MESSAGE 0x0020
 *   HB_FS_MEMVAR   0x0080   HB_FS_LOCAL   0x0200   HB_FS_DEFERRED 0x0800
 *   HB_FS_INITEXIT 0x0018
 *
 * HB_VSCOMP_MEMVAR = HB_VSCOMP_PUBLIC(128) | HB_VSCOMP_PRIVATE(64) = 0xC0
 * which maps to HB_FS_MEMVAR(0x0080) in genc.c's test.
 * ------------------------------------------------------------------------- */
static HB_ULONG hb_llvmScopeFlags( HB_COMP_DECL, PHB_HSYMBOL pSym )
{
   HB_ULONG uFlags = 0;

   if( pSym->szName[ 0 ] == '(' )
   {
      /* Special internal INIT/EXIT symbol: INITEXIT | LOCAL */
      uFlags = HB_FS_INITEXIT | HB_FS_LOCAL;
      return uFlags;
   }

   /* Base scope: PUBLIC / STATIC / INIT / EXIT */
   if( pSym->cScope & HB_FS_STATIC )
      uFlags |= HB_FS_STATIC;
   else if( pSym->cScope & HB_FS_INIT )
      uFlags |= HB_FS_INIT;
   else if( pSym->cScope & HB_FS_EXIT )
      uFlags |= HB_FS_EXIT;
   else
      uFlags |= HB_FS_PUBLIC;

   if( pSym->cScope & HB_VSCOMP_MEMVAR )
      uFlags |= HB_FS_MEMVAR;

   if( pSym->cScope & HB_FS_MESSAGE )
      uFlags |= HB_FS_MESSAGE;

   if( ( pSym->cScope & HB_FS_FIRST ) && ( ! HB_COMP_PARAM->fNoStartUp ) )
      uFlags |= HB_FS_FIRST;

   if( pSym->cScope & HB_FS_LOCAL )
      uFlags |= HB_FS_LOCAL;

   if( pSym->cScope & HB_FS_DEFERRED )
      uFlags |= HB_FS_DEFERRED;

   return uFlags;
}

/* -------------------------------------------------------------------------
 * Straight-line IR helpers (Task 4).
 *
 * The emission is split into two passes so that string-constant global
 * declarations (which must be at module scope in LLVM IR) are emitted
 * BEFORE the function body that references them.
 *
 * Pass A — hb_llvmSLPrecheck:
 *   Runs hb_pcodeAnalyze and returns HB_TRUE iff the function can be
 *   straight-lined (analysis OK + fAllSupported).  Stores the map in
 *   *pMap (caller frees pMap->abLeader on HB_TRUE).
 *
 * Pass B — hb_llvmSLEmitStrings:
 *   Emits @.sl.str.<n> private constants to yyc for every PUSHSTR* opcode
 *   in the function.  Updates *piStrIdx.  Safe to call only when
 *   hb_llvmSLPrecheck returned HB_TRUE.
 *
 * Pass C — hb_llvmSLEmitBody:
 *   Emits the actual function body (entry:, i<off>: blocks, epilogue:).
 *   The string-constant indices used here must match what Pass B emitted
 *   (the caller passes the same *piStrIdx value at the start of the function).
 * ------------------------------------------------------------------------- */

/* Pass A */
static HB_BOOL hb_llvmSLPrecheck( PHB_HFUNC pFunc, HB_PCMAP * pMap )
{
   if( ! hb_pcodeAnalyze( pFunc->pCode, pFunc->nPCodePos, pMap ) )
      return HB_FALSE;
   if( ! pMap->fAllSupported )
   {
      hb_xfree( pMap->abLeader );
      pMap->abLeader = NULL;
      return HB_FALSE;
   }
   return HB_TRUE;
}

/* Pass B — walk pcode, emit @.sl.str.<n> globals.
 * If yyc is NULL, only advances *piStrIdx (dry-run to count strings).
 * If yyc is non-NULL, emits the constants to the file. */
static void hb_llvmSLEmitStrings( FILE * yyc, PHB_HFUNC pFunc,
                                   int * piStrIdx )
{
   const HB_BYTE * pCode   = pFunc->pCode;
   HB_SIZE         nPCSize = pFunc->nPCodePos;
   HB_SIZE         pos     = 0;

   while( pos < nPCSize )
   {
      HB_BYTE op  = pCode[ pos ];
      HB_SIZE len = hb_pcodeInstrLen( &pCode[ pos ] );

      switch( op )
      {
         case HB_P_PUSHSTRSHORT:
         {
            if( yyc )
            {
               HB_SIZE nStr = ( HB_SIZE ) pCode[ pos + 1 ];
               HB_SIZE k;
               fprintf( yyc, "@.sl.str.%d = private constant [%lu x i8] c\"",
                        *piStrIdx, ( unsigned long ) nStr );
               for( k = 0; k < nStr - 1; ++k )
                  hb_llvmEmitByte( yyc, pCode[ pos + 2 + k ] );
               fprintf( yyc, "\\00\"\n" );
            }
            ( *piStrIdx )++;
            break;
         }
         case HB_P_PUSHSTR:
         {
            if( yyc )
            {
               HB_SIZE nStr = ( HB_SIZE ) HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] );
               HB_SIZE k;
               fprintf( yyc, "@.sl.str.%d = private constant [%lu x i8] c\"",
                        *piStrIdx, ( unsigned long ) nStr );
               for( k = 0; k < nStr - 1; ++k )
                  hb_llvmEmitByte( yyc, pCode[ pos + 3 + k ] );
               fprintf( yyc, "\\00\"\n" );
            }
            ( *piStrIdx )++;
            break;
         }
         case HB_P_PUSHSTRLARGE:
         {
            if( yyc )
            {
               HB_SIZE nStr = HB_PCODE_MKUINT24( &pCode[ pos + 1 ] );
               HB_SIZE k;
               fprintf( yyc, "@.sl.str.%d = private constant [%lu x i8] c\"",
                        *piStrIdx, ( unsigned long ) nStr );
               for( k = 0; k < nStr - 1; ++k )
                  hb_llvmEmitByte( yyc, pCode[ pos + 4 + k ] );
               fprintf( yyc, "\\00\"\n" );
            }
            ( *piStrIdx )++;
            break;
         }
         default:
            break;
      }
      pos += len;
   }
}

/* Pass C — emit the function body.
 * iStrBase is the value of *piStrIdx at the start of this function's
 * string emission (i.e. the index of the first @.sl.str.<n> emitted for
 * this function by Pass B). */
static void hb_llvmSLEmitBody( FILE * yyc, PHB_HFUNC pFunc,
                                int iSymCount, int iStrBase )
{
   const HB_BYTE * pCode   = pFunc->pCode;
   HB_SIZE         nPCSize = pFunc->nPCodePos;
   HB_SIZE         pos;
   HB_SIZE         len;
   HB_SIZE         nextOff;
   int             iLocalStr = iStrBase;  /* tracks @.sl.str index per opcode */

   /* -----------------------------------------------------------------------
    * entry: block — emit all alloca slots for conditional-jump logicals first
    * (LLVM requires all allocas to dominate their uses → put them in entry).
    * --------------------------------------------------------------------- */
   fprintf( yyc, "entry:\n" );

   pos = 0;
   while( pos < nPCSize )
   {
      HB_BYTE op = pCode[ pos ];
      len = hb_pcodeInstrLen( &pCode[ pos ] );

      switch( op )
      {
         case HB_P_JUMPFALSENEAR:
         case HB_P_JUMPFALSE:
         case HB_P_JUMPFALSEFAR:
         case HB_P_JUMPTRUENEAR:
         case HB_P_JUMPTRUE:
         case HB_P_JUMPTRUEFAR:
            fprintf( yyc, "  %%jp%lu = alloca i32\n", ( unsigned long ) pos );
            break;
         default:
            break;
      }
      pos += len;
   }

   fprintf( yyc, "  br label %%i0\n" );

   /* -----------------------------------------------------------------------
    * Emit one block per instruction.
    * --------------------------------------------------------------------- */
   pos = 0;
   while( pos < nPCSize )
   {
      char    szNextLabel[ 32 ];   /* "epilogue" or "i<nextOff>" */
      HB_BYTE op = pCode[ pos ];
      len     = hb_pcodeInstrLen( &pCode[ pos ] );
      nextOff = pos + len;

      /* Guard: if the fall-through offset is past the end of pcode there is
       * no i<nextOff> block — branch to epilogue instead (Fix 2). */
      if( nextOff >= nPCSize )
         hb_strncpy( szNextLabel, "epilogue", sizeof( szNextLabel ) - 1 );
      else
         hb_snprintf( szNextLabel, sizeof( szNextLabel ), "i%lu",
                      ( unsigned long ) nextOff );

      fprintf( yyc, "i%lu:\n", ( unsigned long ) pos );

      switch( op )
      {
         case HB_P_LINE:
         case HB_P_NOOP:
            fprintf( yyc, "  br label %%%s\n", szNextLabel );
            break;

         case HB_P_ENDPROC:
            fprintf( yyc, "  br label %%epilogue\n" );
            break;

         case HB_P_JUMP:
         case HB_P_JUMPNEAR:
         case HB_P_JUMPFAR:
         {
            HB_ISIZ disp   = hb_pcodeJumpOffset( &pCode[ pos ] );
            HB_ISIZ target = ( HB_ISIZ ) pos + disp;
            fprintf( yyc, "  br label %%i%ld\n", ( long ) target );
            break;
         }

         case HB_P_JUMPFALSENEAR:
         case HB_P_JUMPFALSE:
         case HB_P_JUMPFALSEFAR:
         {
            HB_ISIZ disp   = hb_pcodeJumpOffset( &pCode[ pos ] );
            HB_ISIZ target = ( HB_ISIZ ) pos + disp;
            fprintf( yyc,
                     "  %%rjp%lu = call i32 @hb_vmsh_poplogical(i32* %%jp%lu)\n"
                     "  %%cjp%lu = icmp ne i32 %%rjp%lu, 0\n"
                     "  br i1 %%cjp%lu, label %%epilogue, label %%jpdone%lu\n"
                     "jpdone%lu:\n"
                     "  %%vjp%lu = load i32, i32* %%jp%lu\n"
                     "  %%bjp%lu = icmp ne i32 %%vjp%lu, 0\n"
                     "  br i1 %%bjp%lu, label %%%s, label %%i%ld\n",
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos,
                     szNextLabel,            /* not taken (value is TRUE)  */
                     ( long ) target         /* taken    (value is FALSE)  */
                     );
            break;
         }

         case HB_P_JUMPTRUENEAR:
         case HB_P_JUMPTRUE:
         case HB_P_JUMPTRUEFAR:
         {
            HB_ISIZ disp   = hb_pcodeJumpOffset( &pCode[ pos ] );
            HB_ISIZ target = ( HB_ISIZ ) pos + disp;
            fprintf( yyc,
                     "  %%rjp%lu = call i32 @hb_vmsh_poplogical(i32* %%jp%lu)\n"
                     "  %%cjp%lu = icmp ne i32 %%rjp%lu, 0\n"
                     "  br i1 %%cjp%lu, label %%epilogue, label %%jpdone%lu\n"
                     "jpdone%lu:\n"
                     "  %%vjp%lu = load i32, i32* %%jp%lu\n"
                     "  %%bjp%lu = icmp ne i32 %%vjp%lu, 0\n"
                     "  br i1 %%bjp%lu, label %%i%ld, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos,
                     ( long ) target,        /* taken    (value is TRUE)   */
                     szNextLabel             /* not taken (value is FALSE) */
                     );
            break;
         }

         case HB_P_PUSHNIL:
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushnil()\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;

         case HB_P_TRUE:
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushlogical(i32 1)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;

         case HB_P_FALSE:
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushlogical(i32 0)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;

         case HB_P_ZERO:
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushint(i32 0)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;

         case HB_P_ONE:
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushint(i32 1)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;

         case HB_P_PUSHBYTE:
         {
            int iVal = ( int ) ( signed char ) pCode[ pos + 1 ];
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushint(i32 %d)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, iVal,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_PUSHINT:
         {
            int iVal = ( int ) HB_PCODE_MKSHORT( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushint(i32 %d)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, iVal,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_PUSHLONG:
         {
            HB_LONG nVal = HB_PCODE_MKLONG( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushlong(i64 %ld)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( long ) nVal,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_PUSHLONGLONG:
         {
            HB_LONGLONG nVal = HB_PCODE_MKLONGLONG( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushlonglong(i64 %lld)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( long long ) nVal,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_PUSHDOUBLE:
         {
            double    dVal = HB_PCODE_MKDOUBLE( &pCode[ pos + 1 ] );
            HB_U64    uBits;
            int       iW   = ( int ) *( const unsigned char * ) &pCode[ pos + 1 + sizeof( double ) ];
            int       iD   = ( int ) *( const unsigned char * ) &pCode[ pos + 2 + sizeof( double ) ];
            /* Use memcpy to type-pun safely (avoids strict-aliasing UB). */
            memcpy( &uBits, &dVal, sizeof( uBits ) );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushdouble(double 0x%016llX, i32 %d, i32 %d)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos,
                     ( unsigned long long ) uBits,
                     iW, iD,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         /* String push — use the @.sl.str.<n> constant emitted in Pass B. */
         case HB_P_PUSHSTRSHORT:
         {
            HB_SIZE nStr  = ( HB_SIZE ) pCode[ pos + 1 ];
            HB_SIZE nPush = nStr > 0 ? nStr - 1 : 0;
            int     iIdx  = iLocalStr++;
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushstring("
                     "i8* getelementptr([%lu x i8], [%lu x i8]* @.sl.str.%d, i32 0, i32 0), "
                     "i64 %lu)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos,
                     ( unsigned long ) nStr, ( unsigned long ) nStr, iIdx,
                     ( unsigned long ) nPush,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_PUSHSTR:
         {
            HB_SIZE nStr  = ( HB_SIZE ) HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] );
            HB_SIZE nPush = nStr > 0 ? nStr - 1 : 0;
            int     iIdx  = iLocalStr++;
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushstring("
                     "i8* getelementptr([%lu x i8], [%lu x i8]* @.sl.str.%d, i32 0, i32 0), "
                     "i64 %lu)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos,
                     ( unsigned long ) nStr, ( unsigned long ) nStr, iIdx,
                     ( unsigned long ) nPush,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_PUSHSTRLARGE:
         {
            HB_SIZE nSize = HB_PCODE_MKUINT24( &pCode[ pos + 1 ] );
            HB_SIZE nPush = nSize > 0 ? nSize - 1 : 0;
            int     iIdx  = iLocalStr++;
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushstring("
                     "i8* getelementptr([%lu x i8], [%lu x i8]* @.sl.str.%d, i32 0, i32 0), "
                     "i64 %lu)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos,
                     ( unsigned long ) nSize, ( unsigned long ) nSize, iIdx,
                     ( unsigned long ) nPush,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_PUSHLOCAL:
         {
            int iLocal = ( int ) HB_PCODE_MKSHORT( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushlocal(i32 %d)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, iLocal,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_PUSHLOCALNEAR:
         {
            int iLocal = ( int ) ( signed char ) pCode[ pos + 1 ];
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushlocal(i32 %d)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, iLocal,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_POPLOCAL:
         {
            int iLocal = ( int ) HB_PCODE_MKSHORT( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_poplocal(i32 %d)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, iLocal,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_POPLOCALNEAR:
         {
            int iLocal = ( int ) ( signed char ) pCode[ pos + 1 ];
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_poplocal(i32 %d)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, iLocal,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_PUSHSTATIC:
         {
            HB_USHORT uiStatic = HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushstatic(i32 %u)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned ) uiStatic,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_POPSTATIC:
         {
            HB_USHORT uiStatic = HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_popstatic(i32 %u)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned ) uiStatic,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_PUSHSYM:
         {
            HB_USHORT uiSym = HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushsymbol(%%HB_SYMB* getelementptr"
                     "([%d x %%HB_SYMB], [%d x %%HB_SYMB]* @symbols_table, i32 0, i32 %u))\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos,
                     iSymCount, iSymCount, ( unsigned ) uiSym,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         /* PUSHFUNCSYM: push symbol + push NIL self slot (two shim calls). */
         case HB_P_PUSHFUNCSYM:
         {
            HB_USHORT uiSym = HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] );
            /* Sub-block a: push the symbol. */
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushsymbol(%%HB_SYMB* getelementptr"
                     "([%d x %%HB_SYMB], [%d x %%HB_SYMB]* @symbols_table, i32 0, i32 %u))\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%ipfs%lua\n"
                     "ipfs%lua:\n"
                     "  %%r%lub = call i32 @hb_vmsh_pushnil()\n"
                     "  %%c%lub = icmp ne i32 %%r%lub, 0\n"
                     "  br i1 %%c%lub, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos,
                     iSymCount, iSymCount, ( unsigned ) uiSym,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos,
                     ( unsigned long ) pos, ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos,
                     szNextLabel );
            break;
         }

         case HB_P_PUSHSYMNEAR:
         {
            unsigned uiSym = ( unsigned ) pCode[ pos + 1 ];
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushsymbol(%%HB_SYMB* getelementptr"
                     "([%d x %%HB_SYMB], [%d x %%HB_SYMB]* @symbols_table, i32 0, i32 %u))\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos,
                     iSymCount, iSymCount, uiSym,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

#define HB_EMIT_NOARG_SHIM( nm ) \
            fprintf( yyc, \
                     "  %%r%lu = call i32 @hb_vmsh_" nm "()\n" \
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n" \
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n", \
                     ( unsigned long ) pos, \
                     ( unsigned long ) pos, ( unsigned long ) pos, \
                     ( unsigned long ) pos, szNextLabel )

         case HB_P_PLUS:           HB_EMIT_NOARG_SHIM( "plus" );           break;
         case HB_P_MINUS:          HB_EMIT_NOARG_SHIM( "minus" );          break;
         case HB_P_MULT:           HB_EMIT_NOARG_SHIM( "mult" );           break;
         case HB_P_DIVIDE:         HB_EMIT_NOARG_SHIM( "divide" );         break;
         case HB_P_MODULUS:        HB_EMIT_NOARG_SHIM( "modulus" );        break;
         case HB_P_POWER:          HB_EMIT_NOARG_SHIM( "power" );          break;
         case HB_P_NEGATE:         HB_EMIT_NOARG_SHIM( "negate" );         break;
         case HB_P_EQUAL:          HB_EMIT_NOARG_SHIM( "equal" );          break;
         case HB_P_EXACTLYEQUAL:   HB_EMIT_NOARG_SHIM( "exactlyequal" );   break;
         case HB_P_NOTEQUAL:       HB_EMIT_NOARG_SHIM( "notequal" );       break;
         case HB_P_LESS:           HB_EMIT_NOARG_SHIM( "less" );           break;
         case HB_P_LESSEQUAL:      HB_EMIT_NOARG_SHIM( "lessequal" );      break;
         case HB_P_GREATER:        HB_EMIT_NOARG_SHIM( "greater" );        break;
         case HB_P_GREATEREQUAL:   HB_EMIT_NOARG_SHIM( "greaterequal" );   break;
         case HB_P_AND:            HB_EMIT_NOARG_SHIM( "and" );            break;
         case HB_P_OR:             HB_EMIT_NOARG_SHIM( "or" );             break;
         case HB_P_NOT:            HB_EMIT_NOARG_SHIM( "not" );            break;
         case HB_P_POP:            HB_EMIT_NOARG_SHIM( "pop" );            break;
         case HB_P_DUPLICATE:      HB_EMIT_NOARG_SHIM( "duplicate" );      break;
         case HB_P_RETVALUE:       HB_EMIT_NOARG_SHIM( "retvalue" );       break;

         /* Group A: FOR loops + compound assignment — 21 no-operand shims */
         case HB_P_FORTEST:        HB_EMIT_NOARG_SHIM( "fortest" );        break;
         case HB_P_INC:            HB_EMIT_NOARG_SHIM( "inc" );            break;
         case HB_P_DEC:            HB_EMIT_NOARG_SHIM( "dec" );            break;
         case HB_P_DUPLUNREF:      HB_EMIT_NOARG_SHIM( "duplunref" );      break;
         case HB_P_PUSHUNREF:      HB_EMIT_NOARG_SHIM( "pushunref" );      break;
         case HB_P_PLUSEQPOP:      HB_EMIT_NOARG_SHIM( "pluseqpop" );      break;
         case HB_P_MINUSEQPOP:     HB_EMIT_NOARG_SHIM( "minuseqpop" );     break;
         case HB_P_MULTEQPOP:      HB_EMIT_NOARG_SHIM( "multeqpop" );      break;
         case HB_P_DIVEQPOP:       HB_EMIT_NOARG_SHIM( "diveqpop" );       break;
         case HB_P_MODEQPOP:       HB_EMIT_NOARG_SHIM( "modeqpop" );       break;
         case HB_P_EXPEQPOP:       HB_EMIT_NOARG_SHIM( "expeqpop" );       break;
         case HB_P_DECEQPOP:       HB_EMIT_NOARG_SHIM( "deceqpop" );       break;
         case HB_P_INCEQPOP:       HB_EMIT_NOARG_SHIM( "inceqpop" );       break;
         case HB_P_PLUSEQ:         HB_EMIT_NOARG_SHIM( "pluseq" );         break;
         case HB_P_MINUSEQ:        HB_EMIT_NOARG_SHIM( "minuseq" );        break;
         case HB_P_MULTEQ:         HB_EMIT_NOARG_SHIM( "multeq" );         break;
         case HB_P_DIVEQ:          HB_EMIT_NOARG_SHIM( "diveq" );          break;
         case HB_P_MODEQ:          HB_EMIT_NOARG_SHIM( "modeq" );          break;
         case HB_P_EXPEQ:          HB_EMIT_NOARG_SHIM( "expeq" );          break;
         case HB_P_DECEQ:          HB_EMIT_NOARG_SHIM( "deceq" );          break;
         case HB_P_INCEQ:          HB_EMIT_NOARG_SHIM( "inceq" );          break;

#undef HB_EMIT_NOARG_SHIM

         /* Group A: 2 ref-push opcodes (2-byte index operand) */
         case HB_P_PUSHLOCALREF:
         {
            int iLocal = ( int ) HB_PCODE_MKSHORT( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushlocalref(i32 %d)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, iLocal,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_PUSHSTATICREF:
         {
            HB_USHORT uiStatic = HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_pushstaticref(i32 %u)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned ) uiStatic,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         /* Group A: 5 local direct-modify opcodes */
         case HB_P_LOCALINC:
         {
            HB_USHORT uiLocal = HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_localinc(i32 %u)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned ) uiLocal,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_LOCALDEC:
         {
            HB_USHORT uiLocal = HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_localdec(i32 %u)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned ) uiLocal,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_LOCALINCPUSH:
         {
            HB_USHORT uiLocal = HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_localincpush(i32 %u)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned ) uiLocal,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_LOCALADDINT:
         {
            HB_USHORT uiLocal  = HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] );
            int       iAddend  = ( int ) HB_PCODE_MKSHORT( &pCode[ pos + 3 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_localaddint(i32 %u, i32 %d)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned ) uiLocal, iAddend,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_LOCALNEARADDINT:
         {
            unsigned uiLocal  = ( unsigned ) pCode[ pos + 1 ];
            int      iAddend  = ( int ) HB_PCODE_MKSHORT( &pCode[ pos + 2 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_localnearaddint(i32 %u, i32 %d)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, uiLocal, iAddend,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_FRAME:
         {
            unsigned uiLocals = ( unsigned ) pCode[ pos + 1 ];
            unsigned ucParams = ( unsigned ) pCode[ pos + 2 ];
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_frame(i32 %u, i32 %u)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, uiLocals, ucParams,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_FUNCTION:
         {
            HB_USHORT uiParams = HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_function(i32 %u)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned ) uiParams,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_FUNCTIONSHORT:
         {
            unsigned uiParams = ( unsigned ) pCode[ pos + 1 ];
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_function(i32 %u)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, uiParams,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_DO:
         {
            HB_USHORT uiParams = HB_PCODE_MKUSHORT( &pCode[ pos + 1 ] );
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_do(i32 %u)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, ( unsigned ) uiParams,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         case HB_P_DOSHORT:
         {
            unsigned uiParams = ( unsigned ) pCode[ pos + 1 ];
            fprintf( yyc,
                     "  %%r%lu = call i32 @hb_vmsh_do(i32 %u)\n"
                     "  %%c%lu = icmp ne i32 %%r%lu, 0\n"
                     "  br i1 %%c%lu, label %%epilogue, label %%%s\n",
                     ( unsigned long ) pos, uiParams,
                     ( unsigned long ) pos, ( unsigned long ) pos,
                     ( unsigned long ) pos, szNextLabel );
            break;
         }

         default:
            /* Should never reach here — hb_llvmSLPrecheck ensured fAllSupported. */
            break;
      }

      pos = nextOff;
   }

   fprintf( yyc, "epilogue:\n" );
   fprintf( yyc, "  ret void\n" );
}

/* -------------------------------------------------------------------------
 * Main entry point.
 * ------------------------------------------------------------------------- */
void hb_compGenLLVMCode( HB_COMP_DECL, PHB_FNAME pFileName )
{
   char        szFileName[ HB_PATH_MAX ];
   PHB_HSYMBOL pSym;
   PHB_HFUNC   pFunc;
   FILE *      yyc;
   int         iSymCount;
   int         iSymIdx;
   int         iStrIdx;   /* module-wide counter for @.sl.str.<n> globals */

   /* -----------------------------------------------------------------------
    * Open output file.
    * --------------------------------------------------------------------- */
   if( ! pFileName->szExtension )
      pFileName->szExtension = ".ll";
   hb_fsFNameMerge( szFileName, pFileName );

   yyc = hb_fopen( szFileName, "w" );
   if( ! yyc )
   {
      hb_compGenError( HB_COMP_PARAM, hb_comp_szErrors, 'E',
                       HB_COMP_ERR_CREATE_OUTPUT, szFileName, NULL );
      return;
   }

   /* -----------------------------------------------------------------------
    * Module header.
    * --------------------------------------------------------------------- */
   fprintf( yyc, "; Harbour LLVM IR - generated from %s\n",
            HB_COMP_PARAM->szFile );
   fprintf( yyc, "%%HB_SYMB = type { i8*, i64, i8*, i8* }\n\n" );
   fprintf( yyc, "declare void @hb_vmExecute(i8*, %%HB_SYMB*)\n" );
   fprintf( yyc, "declare %%HB_SYMB* @hb_vmProcessSymbols("
                 "%%HB_SYMB*, i16, i8*, i32, i16)\n" );

   /* Shim declarations for the straight-line emitter (Task 4). */
   fprintf( yyc, "declare i32 @hb_vmsh_pushnil()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushlogical(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushint(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushlong(i64)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushlonglong(i64)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushdouble(double, i32, i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushstring(i8*, i64)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushlocal(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_poplocal(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushstatic(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_popstatic(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushsymbol(%%HB_SYMB*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_plus()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_minus()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_mult()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_divide()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_modulus()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_power()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_negate()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_equal()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_exactlyequal()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_notequal()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_less()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_lessequal()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_greater()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_greaterequal()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_and()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_or()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_not()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_duplicate()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_frame(i32, i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_function(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_do(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_retvalue()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_poplogical(i32*)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_fortest()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_inc()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_dec()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_duplunref()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushunref()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pluseqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_minuseqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_multeqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_diveqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_modeqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_expeqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_deceqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_inceqpop()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pluseq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_minuseq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_multeq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_diveq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_modeq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_expeq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_deceq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_inceq()\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushlocalref(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_pushstaticref(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_localinc(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_localdec(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_localincpush(i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_localaddint(i32, i32)\n" );
   fprintf( yyc, "declare i32 @hb_vmsh_localnearaddint(i32, i32)\n" );
   if( HB_COMP_PARAM->pInitFunc == NULL )
      fprintf( yyc, "declare void @hb_INITSTATICS()\n" );
   if( HB_COMP_PARAM->pLineFunc == NULL )
      fprintf( yyc, "declare void @hb_INITLINES()\n" );

   /* Declare every external function symbol so the IR verifier accepts the
    * forward references in @symbols_table.  A symbol is an external function
    * when iFunc is set but HB_FS_LOCAL is NOT set (LOCAL means defined here). */
   pSym = HB_COMP_PARAM->symbols.pFirst;
   while( pSym )
   {
      if( pSym->szName[ 0 ] != '(' &&
          !( pSym->cScope & HB_FS_LOCAL ) &&
          pSym->iFunc )
      {
         fprintf( yyc, "declare void @" );
         hb_llvmEmitFuncName( yyc, pSym->szName );
         fprintf( yyc, "()\n" );
      }
      pSym = pSym->pNext;
   }

   fprintf( yyc, "\n" );
   fprintf( yyc, "@symbols = internal global %%HB_SYMB* null\n\n" );

   /* -----------------------------------------------------------------------
    * Pcode globals — one per real function (skip file-decl pseudo-functions).
    * --------------------------------------------------------------------- */
   pFunc = HB_COMP_PARAM->functions.pFirst;
   while( pFunc )
   {
      if( ( pFunc->funFlags & HB_FUNF_FILE_DECL ) == 0 )
      {
         HB_SIZE n;
         /* Determine the LLVM global name for this function's pcode array */
         if( pFunc == HB_COMP_PARAM->pInitFunc )
            fprintf( yyc, "@.pcode.hb_INITSTATICS" );
         else if( pFunc == HB_COMP_PARAM->pLineFunc )
            fprintf( yyc, "@.pcode.hb_INITLINES" );
         else
         {
            fprintf( yyc, "@.pcode." );
            hb_llvmEmitFuncName( yyc, pFunc->szName );
         }
         fprintf( yyc, " = internal constant [%lu x i8] c\"",
                  ( unsigned long ) pFunc->nPCodePos );
         for( n = 0; n < pFunc->nPCodePos; ++n )
            hb_llvmEmitByte( yyc, pFunc->pCode[ n ] );
         fprintf( yyc, "\"\n" );
      }
      pFunc = pFunc->pNext;
   }
   fprintf( yyc, "\n" );

   /* -----------------------------------------------------------------------
    * Symbol-name string constants.
    * Emit one @.sym.<n> for each symbol in HB_COMP_PARAM->symbols.
    * --------------------------------------------------------------------- */
   iSymCount = HB_COMP_PARAM->symbols.iCount;
   iSymIdx   = 0;
   pSym      = HB_COMP_PARAM->symbols.pFirst;
   while( pSym )
   {
      const char * szName = pSym->szName;
      HB_SIZE      nLen   = strlen( szName ) + 1; /* include NUL terminator */
      HB_SIZE      k;

      fprintf( yyc, "@.sym.%d = private constant [%lu x i8] c\"",
               iSymIdx, ( unsigned long ) nLen );
      for( k = 0; k < nLen - 1; ++k )
         hb_llvmEmitByte( yyc, ( HB_BYTE ) szName[ k ] );
      fprintf( yyc, "\\00\"\n" );

      iSymIdx++;
      pSym = pSym->pNext;
   }
   fprintf( yyc, "\n" );

   /* -----------------------------------------------------------------------
    * Module-name string constant.
    * --------------------------------------------------------------------- */
   {
      const char * szModName = HB_COMP_PARAM->szFile;
      HB_SIZE      nModLen   = strlen( szModName ) + 1;
      HB_SIZE      k;

      fprintf( yyc, "@.modname = private constant [%lu x i8] c\"",
               ( unsigned long ) nModLen );
      for( k = 0; k < nModLen - 1; ++k )
         hb_llvmEmitByte( yyc, ( HB_BYTE ) szModName[ k ] );
      fprintf( yyc, "\\00\"\n\n" );
   }

   /* -----------------------------------------------------------------------
    * @symbols_table — [N x %HB_SYMB] initializer.
    * Each element: { name-ptr, i64 <flags>, fn-ptr-or-null, i8* null }
    * --------------------------------------------------------------------- */
   fprintf( yyc, "@symbols_table = internal global [%d x %%HB_SYMB] [\n",
            iSymCount );

   iSymIdx = 0;
   pSym    = HB_COMP_PARAM->symbols.pFirst;
   while( pSym )
   {
      HB_ULONG uFlags   = hb_llvmScopeFlags( HB_COMP_PARAM, pSym );
      HB_SIZE  nSymLen  = strlen( pSym->szName ) + 1; /* include NUL terminator */

      /* Name pointer */
      fprintf( yyc,
               "  %%HB_SYMB { i8* getelementptr([%lu x i8], [%lu x i8]* @.sym.%d, i32 0, i32 0),\n",
               ( unsigned long ) nSymLen,
               ( unsigned long ) nSymLen,
               iSymIdx );

      /* Scope flags as i64 */
      fprintf( yyc, "           i64 %lu,\n", ( unsigned long ) uFlags );

      /* Function pointer or null */
      if( pSym->szName[ 0 ] == '(' )
      {
         /* Internal init symbol: points to hb_INITSTATICS or hb_INITLINES */
         const char * suffix = ( ! memcmp( pSym->szName + 1, "_INITLINES", 10 ) )
                               ? "LINES" : "STATICS";
         fprintf( yyc,
                  "           i8* bitcast(void()* @hb_INIT%s to i8*),\n",
                  suffix );
      }
      else if( pSym->cScope & HB_FS_LOCAL )
      {
         /* Defined in this module */
         fprintf( yyc, "           i8* bitcast(void()* @" );
         hb_llvmEmitFuncName( yyc, pSym->szName );
         fprintf( yyc, " to i8*),\n" );
      }
      else if( !( pSym->cScope & HB_FS_DEFERRED ) && pSym->iFunc )
      {
         /* External function called from this module (e.g. QOUT).
          * Emit a non-null function pointer so the linker pulls the symbol
          * from the runtime library — mirrors genc.c line 338-339. */
         fprintf( yyc, "           i8* bitcast(void()* @" );
         hb_llvmEmitFuncName( yyc, pSym->szName );
         fprintf( yyc, " to i8*),\n" );
      }
      else
      {
         /* Memvar / field / alias / deferred — no function pointer. */
         fprintf( yyc, "           i8* null,\n" );
      }

      /* Reserved slot (always null) */
      fprintf( yyc, "           i8* null }" );

      if( pSym != HB_COMP_PARAM->symbols.pLast )
         fprintf( yyc, "," );
      fprintf( yyc, "\n" );

      iSymIdx++;
      pSym = pSym->pNext;
   }
   fprintf( yyc, "]\n\n" );

   /* -----------------------------------------------------------------------
    * Straight-line string constants — Pass B.
    * Pre-emit all @.sl.str.<n> globals BEFORE the function definitions so
    * they are at module scope (LLVM IR requires global defs before use sites).
    * Only emit for functions that pass hb_llvmSLPrecheck(); special functions
    * (pInitFunc / pLineFunc) always use the fallback.
    * --------------------------------------------------------------------- */
   iStrIdx = 0;
   pFunc   = HB_COMP_PARAM->functions.pFirst;
   while( pFunc )
   {
      if( ( pFunc->funFlags & HB_FUNF_FILE_DECL ) == 0 &&
          pFunc != HB_COMP_PARAM->pInitFunc &&
          pFunc != HB_COMP_PARAM->pLineFunc )
      {
         HB_PCMAP map;
         if( hb_llvmSLPrecheck( pFunc, &map ) )
         {
            hb_llvmSLEmitStrings( yyc, pFunc, &iStrIdx );
            hb_xfree( map.abLeader );
         }
      }
      pFunc = pFunc->pNext;
   }
   fprintf( yyc, "\n" );

   /* -----------------------------------------------------------------------
    * Function definitions — one per real function.
    * Pass C: emit function body (straight-line or fallback).
    * iStrIdx is reset to 0 and advanced in the same order as Pass B so that
    * the @.sl.str.<n> indices match.
    * --------------------------------------------------------------------- */
   iStrIdx = 0;
   pFunc   = HB_COMP_PARAM->functions.pFirst;
   while( pFunc )
   {
      if( ( pFunc->funFlags & HB_FUNF_FILE_DECL ) == 0 )
      {
         const char * pFuncLLVMName;
         HB_BOOL      bSpecial;
         HB_BOOL      bStraightLine = HB_FALSE;
         int          iStrBase      = iStrIdx;

         if( pFunc == HB_COMP_PARAM->pInitFunc )
         {
            pFuncLLVMName = "hb_INITSTATICS";
            bSpecial = HB_TRUE;
         }
         else if( pFunc == HB_COMP_PARAM->pLineFunc )
         {
            pFuncLLVMName = "hb_INITLINES";
            bSpecial = HB_TRUE;
         }
         else
         {
            pFuncLLVMName = NULL;
            bSpecial = HB_FALSE;
         }

         /* For non-special functions, check if straight-line is possible and
          * advance iStrIdx over this function's strings (must mirror Pass B). */
         if( ! bSpecial )
         {
            HB_PCMAP map;
            if( hb_llvmSLPrecheck( pFunc, &map ) )
            {
               bStraightLine = HB_TRUE;
               /* Count strings to advance iStrIdx to the next function's base. */
               hb_llvmSLEmitStrings( NULL, pFunc, &iStrIdx );
               hb_xfree( map.abLeader );
            }
         }

         /* define line */
         if( bSpecial )
            fprintf( yyc, "define void @%s() {\n", pFuncLLVMName );
         else
         {
            fprintf( yyc, "define void @" );
            hb_llvmEmitFuncName( yyc, pFunc->szName );
            fprintf( yyc, "() {\n" );
         }

         if( bStraightLine )
         {
            /* Straight-line body — no %s load needed; shims do not use it. */
            hb_llvmSLEmitBody( yyc, pFunc, iSymCount, iStrBase );
         }
         else
         {
            /* Fallback: load @symbols and call hb_vmExecute. */
            fprintf( yyc, "  %%s = load %%HB_SYMB*, %%HB_SYMB** @symbols\n" );

            if( bSpecial )
            {
               fprintf( yyc,
                        "  call void @hb_vmExecute(i8* getelementptr([%lu x i8], [%lu x i8]* @.pcode.%s, i32 0, i32 0), %%HB_SYMB* %%s)\n",
                        ( unsigned long ) pFunc->nPCodePos,
                        ( unsigned long ) pFunc->nPCodePos,
                        pFuncLLVMName );
            }
            else
            {
               fprintf( yyc,
                        "  call void @hb_vmExecute(i8* getelementptr([%lu x i8], [%lu x i8]* @.pcode.",
                        ( unsigned long ) pFunc->nPCodePos,
                        ( unsigned long ) pFunc->nPCodePos );
               hb_llvmEmitFuncName( yyc, pFunc->szName );
               fprintf( yyc, ", i32 0, i32 0), %%HB_SYMB* %%s)\n" );
            }

            fprintf( yyc, "  ret void\n" );
         }

         fprintf( yyc, "}\n\n" );
      }
      pFunc = pFunc->pNext;
   }

   /* -----------------------------------------------------------------------
    * @hb_vm_SymbolInit constructor.
    * Calls hb_vmProcessSymbols() to register the symbol table, stores the
    * returned base pointer in @symbols so each HB_FUN_* can find it.
    * --------------------------------------------------------------------- */
   {
      unsigned long ulModNameLen = ( unsigned long )( strlen( HB_COMP_PARAM->szFile ) + 1 );
      fprintf( yyc, "define internal void @hb_vm_SymbolInit() {\n" );
      fprintf( yyc,
               "  %%r = call %%HB_SYMB* @hb_vmProcessSymbols(\n"
               "    %%HB_SYMB* getelementptr([%d x %%HB_SYMB], [%d x %%HB_SYMB]* @symbols_table, i32 0, i32 0),\n"
               "    i16 %d,\n"
               "    i8* getelementptr([%lu x i8], [%lu x i8]* @.modname, i32 0, i32 0),\n"
               "    i32 0, i16 3)\n",
               iSymCount, iSymCount,
               iSymCount,
               ulModNameLen,
               ulModNameLen );
      fprintf( yyc, "  store %%HB_SYMB* %%r, %%HB_SYMB** @symbols\n" );
      fprintf( yyc, "  ret void\n}\n\n" );
   }

   /* -----------------------------------------------------------------------
    * @llvm.global_ctors — runs @hb_vm_SymbolInit at load time.
    * --------------------------------------------------------------------- */
   fprintf( yyc,
            "@llvm.global_ctors = appending global [1 x { i32, void()*, i8* }]\n"
            "  [ { i32, void()*, i8* } { i32 65535, void()* @hb_vm_SymbolInit, i8* null } ]\n" );

   fclose( yyc );

   if( ! HB_COMP_PARAM->fQuiet )
      hb_compOutStd( HB_COMP_PARAM, "LLVM IR output done\n" );

   /* -----------------------------------------------------------------------
    * Plan 2: embedded back end — IR -> object -> executable.
    *
    * Calls are made through the g_hb_llvm_backend dispatch table so that
    * tools linking libhbcplr.a without libhbllvm.a (hbrun, hbmk2, …) skip
    * this path gracefully.  harbour.exe links libhbllvm.a which registers
    * the real implementations via hb_llvmBackendRegister() at startup.
    *
    * The intermediate .ll and .o are kept alongside the output (useful for
    * inspection).
    * --------------------------------------------------------------------- */
   if( g_hb_llvm_backend.emitObject != NULL )
   {
      /* I3: Use 4096-byte buffers — hb_llvmRuntimeLibDir builds paths with
       * 4096-byte internals; HB_PATH_MAX (264) can silently truncate a long
       * LLVM install prefix when it is written back through the callback. */
      char szObj[ 4096 ];
      char szExe[ 4096 ];
      char szLibDir[ 4096 ];
      char * pDot;

      hb_strncpy( szObj, szFileName, sizeof( szObj ) - 1 );
      hb_strncpy( szExe, szFileName, sizeof( szExe ) - 1 );

      /* I2: Replace ".ll" extension with ".o" — guard the NULL case
       * defensively; in practice szFileName always ends in ".ll" because the
       * extension is forced above, but if that ever changes we must not
       * silently overwrite the IR file. */
      pDot = strrchr( szObj, '.' );
      if( pDot )
         hb_strncpy( pDot, ".o",
                     sizeof( szObj ) - ( HB_SIZE ) ( pDot - szObj ) - 1 );
      else
      {
         hb_compGenError( HB_COMP_PARAM, hb_comp_szErrors, 'E',
                          HB_COMP_ERR_CREATE_OUTPUT, szObj, NULL );
         return;
      }

      /* I2: Same guard for the ".exe" path. */
      pDot = strrchr( szExe, '.' );
      if( pDot )
         hb_strncpy( pDot, ".exe",
                     sizeof( szExe ) - ( HB_SIZE ) ( pDot - szExe ) - 1 );
      else
      {
         hb_compGenError( HB_COMP_PARAM, hb_comp_szErrors, 'E',
                          HB_COMP_ERR_CREATE_OUTPUT, szExe, NULL );
         return;
      }

      /* Runtime archives live next to harbour.exe: <prefix>/lib/win/mingw64 */
      g_hb_llvm_backend.runtimeLibDir( szLibDir, sizeof( szLibDir ) );

      /* I1: Guard against GetModuleFileNameA failure leaving szLibDir as "".
       * Proceeding with an empty -L path would silently link against the
       * wrong (or no) runtime libraries. */
      if( szLibDir[ 0 ] == '\0' )
      {
         hb_compGenError( HB_COMP_PARAM, hb_comp_szErrors, 'E',
                          HB_COMP_ERR_CREATE_OUTPUT,
                          "(runtime lib dir unknown)", NULL );
         return;
      }

      if( g_hb_llvm_backend.emitObject( szFileName, szObj ) == 0 )
      {
         if( g_hb_llvm_backend.linkExe( szObj, szLibDir, szExe ) == 0 )
         {
            if( ! HB_COMP_PARAM->fQuiet )
               hb_compOutStd( HB_COMP_PARAM, "LLVM: executable created\n" );
         }
         else
            hb_compGenError( HB_COMP_PARAM, hb_comp_szErrors, 'E',
                             HB_COMP_ERR_CREATE_OUTPUT, szExe, NULL );
      }
      else
         hb_compGenError( HB_COMP_PARAM, hb_comp_szErrors, 'E',
                          HB_COMP_ERR_CREATE_OUTPUT, szObj, NULL );
   }
}
