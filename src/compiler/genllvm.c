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
   if( HB_COMP_PARAM->pInitFunc == NULL )
      fprintf( yyc, "declare void @hb_INITSTATICS()\n" );
   if( HB_COMP_PARAM->pLineFunc == NULL )
      fprintf( yyc, "declare void @hb_INITLINES()\n" );
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
      else
      {
         /* Plan 1: all external symbols intentionally get an i8* null
          * function pointer.  iFunc is deliberately not consulted here;
          * the VM resolves external symbols by name at load time. */
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
    * Function definitions — one per real function.
    * --------------------------------------------------------------------- */
   pFunc = HB_COMP_PARAM->functions.pFirst;
   while( pFunc )
   {
      if( ( pFunc->funFlags & HB_FUNF_FILE_DECL ) == 0 )
      {
         const char * pFuncLLVMName;  /* plain C string when a special name applies */
         HB_BOOL      bSpecial;

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

         /* define line */
         if( bSpecial )
            fprintf( yyc, "define void @%s() {\n", pFuncLLVMName );
         else
         {
            fprintf( yyc, "define void @" );
            hb_llvmEmitFuncName( yyc, pFunc->szName );
            fprintf( yyc, "() {\n" );
         }

         fprintf( yyc, "  %%s = load %%HB_SYMB*, %%HB_SYMB** @symbols\n" );

         /* call line — pcode array reference must match the global name emitted above */
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

         fprintf( yyc, "  ret void\n}\n\n" );
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
}
