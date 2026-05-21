/*
 * Harbour VM op shim — exported one-call-per-opcode entry points used by
 * the straight-line LLVM backend. Each function performs exactly what the
 * corresponding hb_vmExecute() switch case does, and returns the current
 * action request (0 = continue, non-zero = unwind to the function epilogue).
 */
#ifndef HB_VMSH_H_
#define HB_VMSH_H_

#include "hbvmpub.h"

HB_EXTERN_BEGIN

/* literals */
extern HB_EXPORT int hb_vmsh_pushnil( void );
extern HB_EXPORT int hb_vmsh_pushlogical( HB_BOOL fValue );
extern HB_EXPORT int hb_vmsh_pushint( int iValue );
extern HB_EXPORT int hb_vmsh_pushlong( HB_MAXINT nValue );
extern HB_EXPORT int hb_vmsh_pushlonglong( HB_LONGLONG llValue );
extern HB_EXPORT int hb_vmsh_pushdouble( double dValue, int iWidth, int iDec );
extern HB_EXPORT int hb_vmsh_pushstring( const char * szText, HB_SIZE nLen );

/* locals / statics */
extern HB_EXPORT int hb_vmsh_pushlocal( int iLocal );
extern HB_EXPORT int hb_vmsh_poplocal( int iLocal );
extern HB_EXPORT int hb_vmsh_pushstatic( int iStatic );
extern HB_EXPORT int hb_vmsh_popstatic( int iStatic );

/* symbol push (pSym points into the module @symbols_table) */
extern HB_EXPORT int hb_vmsh_pushsymbol( PHB_SYMB pSym );

/* arithmetic (operate on the VM stack, like the interpreter cases) */
extern HB_EXPORT int hb_vmsh_plus( void );
extern HB_EXPORT int hb_vmsh_minus( void );
extern HB_EXPORT int hb_vmsh_mult( void );
extern HB_EXPORT int hb_vmsh_divide( void );
extern HB_EXPORT int hb_vmsh_modulus( void );
extern HB_EXPORT int hb_vmsh_power( void );
extern HB_EXPORT int hb_vmsh_negate( void );

/* comparisons */
extern HB_EXPORT int hb_vmsh_equal( void );
extern HB_EXPORT int hb_vmsh_exactlyequal( void );
extern HB_EXPORT int hb_vmsh_notequal( void );
extern HB_EXPORT int hb_vmsh_less( void );
extern HB_EXPORT int hb_vmsh_lessequal( void );
extern HB_EXPORT int hb_vmsh_greater( void );
extern HB_EXPORT int hb_vmsh_greaterequal( void );

/* logical */
extern HB_EXPORT int hb_vmsh_and( void );
extern HB_EXPORT int hb_vmsh_or( void );
extern HB_EXPORT int hb_vmsh_not( void );

/* stack misc */
extern HB_EXPORT int hb_vmsh_pop( void );
extern HB_EXPORT int hb_vmsh_duplicate( void );

/* frame / parameters */
extern HB_EXPORT int hb_vmsh_frame( int iLocals, int iParams );

/* calls — iParams is the operand of HB_P_FUNCTION/DO; the symbol was
 * already pushed by a preceding HB_P_PUSHSYM. */
extern HB_EXPORT int hb_vmsh_function( int iParams );
extern HB_EXPORT int hb_vmsh_do( int iParams );

/* return value */
extern HB_EXPORT int hb_vmsh_retvalue( void );

/* For conditional jumps: pop the top item as a logical and report it.
 * *pfValue receives the popped logical; the return value is the action
 * request (a non-logical top item raises an error -> non-zero request). */
extern HB_EXPORT int hb_vmsh_poplogical( HB_BOOL * pfValue );

/* --- group A: FOR loops + compound assignment --- */
extern HB_EXPORT int hb_vmsh_fortest( void );
extern HB_EXPORT int hb_vmsh_inc( void );
extern HB_EXPORT int hb_vmsh_dec( void );
extern HB_EXPORT int hb_vmsh_duplunref( void );
extern HB_EXPORT int hb_vmsh_pushunref( void );
extern HB_EXPORT int hb_vmsh_pluseqpop( void );
extern HB_EXPORT int hb_vmsh_minuseqpop( void );
extern HB_EXPORT int hb_vmsh_multeqpop( void );
extern HB_EXPORT int hb_vmsh_diveqpop( void );
extern HB_EXPORT int hb_vmsh_modeqpop( void );
extern HB_EXPORT int hb_vmsh_expeqpop( void );
extern HB_EXPORT int hb_vmsh_deceqpop( void );
extern HB_EXPORT int hb_vmsh_inceqpop( void );
extern HB_EXPORT int hb_vmsh_pluseq( void );
extern HB_EXPORT int hb_vmsh_minuseq( void );
extern HB_EXPORT int hb_vmsh_multeq( void );
extern HB_EXPORT int hb_vmsh_diveq( void );
extern HB_EXPORT int hb_vmsh_modeq( void );
extern HB_EXPORT int hb_vmsh_expeq( void );
extern HB_EXPORT int hb_vmsh_deceq( void );
extern HB_EXPORT int hb_vmsh_inceq( void );
extern HB_EXPORT int hb_vmsh_pushlocalref( int iLocal );
extern HB_EXPORT int hb_vmsh_pushstaticref( int iStatic );
extern HB_EXPORT int hb_vmsh_localinc( int iLocal );
extern HB_EXPORT int hb_vmsh_localdec( int iLocal );
extern HB_EXPORT int hb_vmsh_localincpush( int iLocal );
extern HB_EXPORT int hb_vmsh_localaddint( int iLocal, int iAdd );
extern HB_EXPORT int hb_vmsh_localnearaddint( int iLocal, int iAdd );

/* --- group B: arrays + hashes --- */
extern HB_EXPORT int hb_vmsh_arraypush( void );
extern HB_EXPORT int hb_vmsh_arraypushref( void );
extern HB_EXPORT int hb_vmsh_arraypop( void );
extern HB_EXPORT int hb_vmsh_pushaparams( void );
extern HB_EXPORT int hb_vmsh_arraydim( int iCount );
extern HB_EXPORT int hb_vmsh_arraygen( int iCount );
extern HB_EXPORT int hb_vmsh_hashgen( int iCount );

/* --- group C: RDD fields, memvars, aliases --- */
extern HB_EXPORT int hb_vmsh_pushalias( void );
extern HB_EXPORT int hb_vmsh_popalias( void );
extern HB_EXPORT int hb_vmsh_swapalias( void );
extern HB_EXPORT int hb_vmsh_pushfield( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_popfield( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_pushmemvar( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_pushmemvarref( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_popmemvar( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_pushvariable( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_popvariable( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_pushaliasedfield( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_popaliasedfield( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_pushaliasedvar( PHB_SYMB pSym );
extern HB_EXPORT int hb_vmsh_popaliasedvar( PHB_SYMB pSym );

HB_EXTERN_END

#endif
