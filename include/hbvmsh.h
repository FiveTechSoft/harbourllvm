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
extern HB_EXPORT int hb_vmsh_pushdouble( double dValue, int iWidth, int iDec );
extern HB_EXPORT int hb_vmsh_pushstring( const char * szText, HB_SIZE nLen );

/* locals / statics */
extern HB_EXPORT int hb_vmsh_pushlocal( int iLocal );
extern HB_EXPORT int hb_vmsh_poplocal( int iLocal );
extern HB_EXPORT int hb_vmsh_pushstatic( HB_USHORT uiStatic );
extern HB_EXPORT int hb_vmsh_popstatic( HB_USHORT uiStatic );

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
extern HB_EXPORT int hb_vmsh_frame( HB_USHORT uiLocals, unsigned char ucParams );

/* calls — uiParams is the operand of HB_P_FUNCTION/DO; the symbol was
 * already pushed by a preceding HB_P_PUSHSYM. */
extern HB_EXPORT int hb_vmsh_function( HB_USHORT uiParams );
extern HB_EXPORT int hb_vmsh_do( HB_USHORT uiParams );

/* return value */
extern HB_EXPORT int hb_vmsh_retvalue( void );

/* For conditional jumps: pop the top item as a logical and report it.
 * *pfValue receives the popped logical; the return value is the action
 * request (a non-logical top item raises an error -> non-zero request). */
extern HB_EXPORT int hb_vmsh_poplogical( HB_BOOL * pfValue );

HB_EXTERN_END

#endif
