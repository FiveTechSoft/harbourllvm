/*
 * Harbour pcode opcode decoder table for the straight-line LLVM backend.
 *
 * Each entry maps one HB_P_* opcode (indexed by numeric value) to:
 *   kind       - how to compute the instruction's total byte length
 *   nLen       - for HB_PCK_FIXED: total bytes (opcode + operands)
 *   fSupported - HB_TRUE if the straight-line emitter handles this opcode
 *
 * Lengths verified against the hb_vmExecute() switch in src/vm/hvm.c.
 * The `pCode += N` (or `pCode++`) at the end of each case gives N.
 *
 * Copyright 2026 Antonio Linares / HarbourLLVM contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#include "hb_pcdec.h"
#include "hbapi.h"   /* hb_xgrabz / hb_xfree */

/*
 * Straight-line emitter "in scope" subset (fSupported = HB_TRUE):
 *
 * Plan 3 baseline:
 *   HB_P_AND, HB_P_EQUAL, HB_P_EXACTLYEQUAL, HB_P_FALSE,
 *   HB_P_FUNCTION, HB_P_FUNCTIONSHORT, HB_P_FRAME, HB_P_GREATER,
 *   HB_P_GREATEREQUAL, HB_P_DIVIDE, HB_P_DO, HB_P_DOSHORT, HB_P_DUPLICATE,
 *   HB_P_JUMPNEAR, HB_P_JUMP, HB_P_JUMPFAR,
 *   HB_P_JUMPFALSENEAR, HB_P_JUMPFALSE, HB_P_JUMPFALSEFAR,
 *   HB_P_JUMPTRUENEAR, HB_P_JUMPTRUE, HB_P_JUMPTRUEFAR,
 *   HB_P_LESSEQUAL, HB_P_LESS, HB_P_LINE,
 *   HB_P_MINUS, HB_P_MODULUS, HB_P_MULT, HB_P_NEGATE, HB_P_NOOP,
 *   HB_P_NOT, HB_P_NOTEQUAL, HB_P_OR,
 *   HB_P_PLUS, HB_P_POP, HB_P_POWER,
 *   HB_P_PUSHBYTE, HB_P_PUSHINT, HB_P_PUSHLOCAL, HB_P_PUSHLOCALNEAR,
 *   HB_P_PUSHLONG, HB_P_PUSHLONGLONG, HB_P_PUSHDOUBLE, HB_P_PUSHNIL,
 *   HB_P_PUSHSTATIC, HB_P_PUSHSTR, HB_P_PUSHSTRSHORT, HB_P_PUSHSTRLARGE,
 *   HB_P_PUSHSYM, HB_P_PUSHSYMNEAR, HB_P_PUSHFUNCSYM,
 *   HB_P_RETVALUE, HB_P_ENDPROC,
 *   HB_P_POPLOCAL, HB_P_POPLOCALNEAR, HB_P_POPSTATIC,
 *   HB_P_TRUE, HB_P_ZERO, HB_P_ONE
 *
 * Group A additions (FOR loops + compound assignment):
 *   HB_P_FORTEST, HB_P_INC, HB_P_DEC,
 *   HB_P_DUPLUNREF, HB_P_PUSHUNREF,
 *   HB_P_PUSHLOCALREF, HB_P_PUSHSTATICREF,
 *   HB_P_PLUSEQPOP, HB_P_MINUSEQPOP, HB_P_MULTEQPOP, HB_P_DIVEQPOP,
 *   HB_P_MODEQPOP, HB_P_EXPEQPOP,
 *   HB_P_DECEQPOP, HB_P_INCEQPOP,
 *   HB_P_PLUSEQ, HB_P_MINUSEQ, HB_P_MULTEQ, HB_P_DIVEQ,
 *   HB_P_MODEQ, HB_P_EXPEQ,
 *   HB_P_DECEQ, HB_P_INCEQ,
 *   HB_P_LOCALDEC, HB_P_LOCALINC, HB_P_LOCALINCPUSH,
 *   HB_P_LOCALADDINT, HB_P_LOCALNEARADDINT
 *
 * Group B additions (arrays + hashes):
 *   HB_P_ARRAYPUSH, HB_P_ARRAYPOP, HB_P_ARRAYDIM, HB_P_ARRAYGEN,
 *   HB_P_ARRAYPUSHREF, HB_P_HASHGEN, HB_P_PUSHAPARAMS
 *
 * Group C additions (RDD fields, memvars, aliases):
 *   HB_P_POPALIAS, HB_P_POPALIASEDFIELD, HB_P_POPALIASEDFIELDNEAR,
 *   HB_P_POPALIASEDVAR, HB_P_POPFIELD, HB_P_POPMEMVAR, HB_P_POPVARIABLE,
 *   HB_P_PUSHALIAS, HB_P_PUSHALIASEDFIELD, HB_P_PUSHALIASEDFIELDNEAR,
 *   HB_P_PUSHALIASEDVAR, HB_P_PUSHFIELD, HB_P_PUSHMEMVAR,
 *   HB_P_PUSHMEMVARREF, HB_P_PUSHVARIABLE, HB_P_SWAPALIAS
 *
 * Group D additions (OOP messages):
 *   HB_P_FUNCPTR, HB_P_MESSAGE, HB_P_PUSHSELF,
 *   HB_P_SEND, HB_P_SENDSHORT,
 *   HB_P_WITHOBJECTSTART, HB_P_WITHOBJECTMESSAGE, HB_P_WITHOBJECTEND,
 *   HB_P_PUSHOVARREF
 *
 * Group E additions (FOR EACH loops):
 *   HB_P_ENUMSTART, HB_P_ENUMNEXT, HB_P_ENUMPREV, HB_P_ENUMEND
 *
 * Group F additions (SWITCH):
 *   HB_P_SWITCH
 *
 * Group G additions (codeblocks):
 *   HB_P_PUSHBLOCK, HB_P_PUSHBLOCKSHORT, HB_P_PUSHBLOCKLARGE
 *
 * Group H additions (macros):
 *   HB_P_MACROPOP, HB_P_MACROPOPALIASED, HB_P_MACROPUSH,
 *   HB_P_MACROARRAYGEN, HB_P_MACROPUSHLIST, HB_P_MACROPUSHINDEX,
 *   HB_P_MACROPUSHPARE, HB_P_MACROPUSHALIASED, HB_P_MACROSYMBOL,
 *   HB_P_MACROTEXT, HB_P_MACROFUNC, HB_P_MACRODO, HB_P_MACROPUSHREF,
 *   HB_P_MACROSEND
 */

/* Compile-time size check — must have exactly HB_P_LAST_PCODE entries. */
#define HB_PCDEC_ENTRY_COUNT  181   /* == HB_P_LAST_PCODE */

const HB_PCINFO hb_pcInfo[] =
{
   /* 0  HB_P_AND            */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 1  HB_P_ARRAYPUSH      */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 2  HB_P_ARRAYPOP       */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 3  HB_P_ARRAYDIM       */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 4  HB_P_ARRAYGEN       */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 5  HB_P_EQUAL          */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 6  HB_P_ENDBLOCK       */ { HB_PCK_FIXED,    1,  HB_FALSE },
   /* 7  HB_P_ENDPROC        */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 8  HB_P_EXACTLYEQUAL   */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 9  HB_P_FALSE          */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 10 HB_P_FORTEST        */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 11 HB_P_FUNCTION       */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 12 HB_P_FUNCTIONSHORT  */ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 13 HB_P_FRAME          */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 14 HB_P_FUNCPTR        */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 15 HB_P_GREATER        */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 16 HB_P_GREATEREQUAL   */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 17 HB_P_DEC            */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 18 HB_P_DIVIDE         */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 19 HB_P_DO             */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 20 HB_P_DOSHORT        */ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 21 HB_P_DUPLICATE      */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 22 HB_P_PUSHTIMESTAMP  */ { HB_PCK_FIXED,    9,  HB_FALSE },
   /* 23 HB_P_INC            */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 24 HB_P_INSTRING       */ { HB_PCK_FIXED,    1,  HB_FALSE },
   /* 25 HB_P_JUMPNEAR       */ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 26 HB_P_JUMP           */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 27 HB_P_JUMPFAR        */ { HB_PCK_FIXED,    4,  HB_TRUE  },
   /* 28 HB_P_JUMPFALSENEAR  */ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 29 HB_P_JUMPFALSE      */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 30 HB_P_JUMPFALSEFAR   */ { HB_PCK_FIXED,    4,  HB_TRUE  },
   /* 31 HB_P_JUMPTRUENEAR   */ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 32 HB_P_JUMPTRUE       */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 33 HB_P_JUMPTRUEFAR    */ { HB_PCK_FIXED,    4,  HB_TRUE  },
   /* 34 HB_P_LESSEQUAL      */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 35 HB_P_LESS           */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 36 HB_P_LINE           */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 37 HB_P_LOCALNAME      */ { HB_PCK_UNKNOWN,  0,  HB_FALSE }, /* null-terminated string at pCode+3 */
   /* 38 HB_P_MACROPOP       */ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 39 HB_P_MACROPOPALIASED*/ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 40 HB_P_MACROPUSH      */ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 41 HB_P_MACROARRAYGEN  */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 42 HB_P_MACROPUSHLIST  */ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 43 HB_P_MACROPUSHINDEX */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 44 HB_P_MACROPUSHPARE  */ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 45 HB_P_MACROPUSHALIASED*/{HB_PCK_FIXED,     2,  HB_TRUE  },
   /* 46 HB_P_MACROSYMBOL    */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 47 HB_P_MACROTEXT      */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 48 HB_P_MESSAGE        */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 49 HB_P_MINUS          */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 50 HB_P_MODULUS        */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 51 HB_P_MODULENAME     */ { HB_PCK_UNKNOWN,  0,  HB_FALSE }, /* null-terminated string at pCode+1 */
   /* 52 HB_P_MMESSAGE       */ { HB_PCK_UNKNOWN,  0,  HB_FALSE }, /* sizeof(PHB_DYNS)+1, pointer-width dependent */
   /* 53 HB_P_MPOPALIASEDFIELD*/{HB_PCK_UNKNOWN,   0,  HB_FALSE },
   /* 54 HB_P_MPOPALIASEDVAR */ { HB_PCK_UNKNOWN,  0,  HB_FALSE },
   /* 55 HB_P_MPOPFIELD      */ { HB_PCK_UNKNOWN,  0,  HB_FALSE },
   /* 56 HB_P_MPOPMEMVAR     */ { HB_PCK_UNKNOWN,  0,  HB_FALSE },
   /* 57 HB_P_MPUSHALIASEDFIELD*/{HB_PCK_UNKNOWN,  0,  HB_FALSE },
   /* 58 HB_P_MPUSHALIASEDVAR*/ { HB_PCK_UNKNOWN,  0,  HB_FALSE },
   /* 59 HB_P_MPUSHBLOCK     */ { HB_PCK_VARBLOCK, 0,  HB_FALSE }, /* 2-byte size operand */
   /* 60 HB_P_MPUSHFIELD     */ { HB_PCK_UNKNOWN,  0,  HB_FALSE },
   /* 61 HB_P_MPUSHMEMVAR    */ { HB_PCK_UNKNOWN,  0,  HB_FALSE },
   /* 62 HB_P_MPUSHMEMVARREF */ { HB_PCK_UNKNOWN,  0,  HB_FALSE },
   /* 63 HB_P_MPUSHSYM       */ { HB_PCK_UNKNOWN,  0,  HB_FALSE },
   /* 64 HB_P_MPUSHVARIABLE  */ { HB_PCK_UNKNOWN,  0,  HB_FALSE },
   /* 65 HB_P_MULT           */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 66 HB_P_NEGATE         */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 67 HB_P_NOOP           */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 68 HB_P_NOT            */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 69 HB_P_NOTEQUAL       */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 70 HB_P_OR             */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 71 HB_P_PARAMETER      */ { HB_PCK_FIXED,    4,  HB_FALSE },
   /* 72 HB_P_PLUS           */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 73 HB_P_POP            */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 74 HB_P_POPALIAS       */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 75 HB_P_POPALIASEDFIELD*/ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 76 HB_P_POPALIASEDFIELDNEAR*/{HB_PCK_FIXED,  2,  HB_TRUE  },
   /* 77 HB_P_POPALIASEDVAR  */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 78 HB_P_POPFIELD       */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 79 HB_P_POPLOCAL       */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 80 HB_P_POPLOCALNEAR   */ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 81 HB_P_POPMEMVAR      */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 82 HB_P_POPSTATIC      */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 83 HB_P_POPVARIABLE    */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 84 HB_P_POWER          */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 85 HB_P_PUSHALIAS      */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 86 HB_P_PUSHALIASEDFIELD*/{HB_PCK_FIXED,     3,  HB_TRUE  },
   /* 87 HB_P_PUSHALIASEDFIELDNEAR*/{HB_PCK_FIXED, 2,  HB_TRUE  },
   /* 88 HB_P_PUSHALIASEDVAR */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 89 HB_P_PUSHBLOCK      */ { HB_PCK_VARBLOCK, 0,  HB_TRUE  }, /* 2-byte size operand */
   /* 90 HB_P_PUSHBLOCKSHORT */ { HB_PCK_VARBLOCK, 0,  HB_TRUE  }, /* 1-byte size operand */
   /* 91 HB_P_PUSHFIELD      */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 92 HB_P_PUSHBYTE       */ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 93 HB_P_PUSHINT        */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 94 HB_P_PUSHLOCAL      */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 95 HB_P_PUSHLOCALNEAR  */ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 96 HB_P_PUSHLOCALREF   */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 97 HB_P_PUSHLONG       */ { HB_PCK_FIXED,    5,  HB_TRUE  },
   /* 98 HB_P_PUSHMEMVAR     */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 99 HB_P_PUSHMEMVARREF  */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 100 HB_P_PUSHNIL       */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 101 HB_P_PUSHDOUBLE    */ { HB_PCK_FIXED,   11,  HB_TRUE  }, /* 3 + sizeof(double)=8 */
   /* 102 HB_P_PUSHSELF      */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 103 HB_P_PUSHSTATIC    */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 104 HB_P_PUSHSTATICREF */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 105 HB_P_PUSHSTR       */ { HB_PCK_STR2,     0,  HB_TRUE  }, /* 2-byte length prefix */
   /* 106 HB_P_PUSHSTRSHORT  */ { HB_PCK_STR1,     0,  HB_TRUE  }, /* 1-byte length prefix */
   /* 107 HB_P_PUSHSYM       */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 108 HB_P_PUSHSYMNEAR   */ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 109 HB_P_PUSHVARIABLE  */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 110 HB_P_RETVALUE      */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 111 HB_P_SEND          */ { HB_PCK_FIXED,    3,  HB_TRUE  }, /* instr is 3 bytes; interpreter may eat following POP */
   /* 112 HB_P_SENDSHORT     */ { HB_PCK_FIXED,    2,  HB_TRUE  },
   /* 113 HB_P_SEQBEGIN      */ { HB_PCK_FIXED,    4,  HB_FALSE },
   /* 114 HB_P_SEQEND        */ { HB_PCK_FIXED,    4,  HB_FALSE }, /* 4-byte instr; interp uses operand as jump offset */
   /* 115 HB_P_SEQRECOVER    */ { HB_PCK_FIXED,    1,  HB_FALSE },
   /* 116 HB_P_SFRAME        */ { HB_PCK_FIXED,    3,  HB_FALSE },
   /* 117 HB_P_STATICS       */ { HB_PCK_FIXED,    5,  HB_FALSE },
   /* 118 HB_P_STATICNAME    */ { HB_PCK_UNKNOWN,  0,  HB_FALSE }, /* null-terminated string at pCode+4 */
   /* 119 HB_P_SWAPALIAS     */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 120 HB_P_TRUE          */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 121 HB_P_ZERO          */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 122 HB_P_ONE           */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 123 HB_P_MACROFUNC     */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 124 HB_P_MACRODO       */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 125 HB_P_MPUSHSTR      */ { HB_PCK_STR2,     0,  HB_FALSE }, /* 2-byte length prefix, macro-compiled string */
   /* 126 HB_P_LOCALNEARADDINT*/{HB_PCK_FIXED,     4,  HB_TRUE  },
   /* 127 HB_P_MACROPUSHREF  */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 128 HB_P_PUSHLONGLONG  */ { HB_PCK_FIXED,    9,  HB_TRUE  },
   /* 129 HB_P_ENUMSTART     */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 130 HB_P_ENUMNEXT      */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 131 HB_P_ENUMPREV      */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 132 HB_P_ENUMEND       */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 133 HB_P_SWITCH        */ { HB_PCK_SWITCH,   0,  HB_TRUE  }, /* variable: hb_vmSwitch table */
   /* 134 HB_P_PUSHDATE      */ { HB_PCK_FIXED,    5,  HB_FALSE },
   /* 135 HB_P_PLUSEQPOP     */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 136 HB_P_MINUSEQPOP    */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 137 HB_P_MULTEQPOP     */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 138 HB_P_DIVEQPOP      */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 139 HB_P_PLUSEQ        */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 140 HB_P_MINUSEQ       */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 141 HB_P_MULTEQ        */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 142 HB_P_DIVEQ         */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 143 HB_P_WITHOBJECTSTART*/{HB_PCK_FIXED,     1,  HB_TRUE  },
   /* 144 HB_P_WITHOBJECTMESSAGE*/{HB_PCK_FIXED,   3,  HB_TRUE  },
   /* 145 HB_P_WITHOBJECTEND */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 146 HB_P_MACROSEND     */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 147 HB_P_PUSHOVARREF   */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 148 HB_P_ARRAYPUSHREF  */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 149 HB_P_VFRAME        */ { HB_PCK_FIXED,    3,  HB_FALSE },
   /* 150 HB_P_LARGEFRAME    */ { HB_PCK_FIXED,    4,  HB_FALSE },
   /* 151 HB_P_LARGEVFRAME   */ { HB_PCK_FIXED,    4,  HB_FALSE },
   /* 152 HB_P_PUSHSTRHIDDEN */ { HB_PCK_UNKNOWN,  0,  HB_FALSE }, /* 4 + encoded-size; size modifiable by decoder */
   /* 153 HB_P_LOCALADDINT   */ { HB_PCK_FIXED,    5,  HB_TRUE  },
   /* 154 HB_P_MODEQPOP      */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 155 HB_P_EXPEQPOP      */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 156 HB_P_MODEQ         */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 157 HB_P_EXPEQ         */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 158 HB_P_DUPLUNREF     */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 159 HB_P_MPUSHBLOCKLARGE*/{HB_PCK_VARBLOCK,  0,  HB_FALSE }, /* 3-byte size operand */
   /* 160 HB_P_MPUSHSTRLARGE */ { HB_PCK_STR3,     0,  HB_FALSE }, /* 3-byte length prefix, macro-compiled string */
   /* 161 HB_P_PUSHBLOCKLARGE*/ { HB_PCK_VARBLOCK, 0,  HB_TRUE  }, /* 3-byte size operand */
   /* 162 HB_P_PUSHSTRLARGE  */ { HB_PCK_STR3,     0,  HB_TRUE  }, /* 3-byte length prefix */
   /* 163 HB_P_SWAP          */ { HB_PCK_FIXED,    2,  HB_FALSE },
   /* 164 HB_P_PUSHVPARAMS   */ { HB_PCK_FIXED,    1,  HB_FALSE },
   /* 165 HB_P_PUSHUNREF     */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 166 HB_P_SEQALWAYS     */ { HB_PCK_FIXED,    4,  HB_FALSE },
   /* 167 HB_P_ALWAYSBEGIN   */ { HB_PCK_FIXED,    4,  HB_FALSE },
   /* 168 HB_P_ALWAYSEND     */ { HB_PCK_FIXED,    1,  HB_FALSE },
   /* 169 HB_P_DECEQPOP      */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 170 HB_P_INCEQPOP      */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 171 HB_P_DECEQ         */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 172 HB_P_INCEQ         */ { HB_PCK_FIXED,    1,  HB_TRUE  },
   /* 173 HB_P_LOCALDEC      */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 174 HB_P_LOCALINC      */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 175 HB_P_LOCALINCPUSH  */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 176 HB_P_PUSHFUNCSYM   */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 177 HB_P_HASHGEN       */ { HB_PCK_FIXED,    3,  HB_TRUE  },
   /* 178 HB_P_SEQBLOCK      */ { HB_PCK_FIXED,    1,  HB_FALSE },
   /* 179 HB_P_THREADSTATICS */ { HB_PCK_UNKNOWN,  0,  HB_FALSE }, /* 3 + 2*count; count from 2-byte operand */
   /* 180 HB_P_PUSHAPARAMS   */ { HB_PCK_FIXED,    1,  HB_TRUE  },
};

/* Verify the table has exactly HB_P_LAST_PCODE entries at compile time. */
typedef char hb_pcdec_size_check[
   ( sizeof( hb_pcInfo ) / sizeof( hb_pcInfo[ 0 ] ) == HB_PCDEC_ENTRY_COUNT )
   ? 1 : -1 ];

/* The HB_P_PUSHDOUBLE row hardcodes length 11 ( 3 + sizeof(double) ).
 * Assert the assumption so a non-8-byte-double target fails to compile
 * rather than silently mis-decoding a supported opcode. */
typedef char hb_pcdec_double_check[ ( sizeof( double ) == 8 ) ? 1 : -1 ];

HB_SIZE hb_pcodeInstrLen( const HB_BYTE * pCode )
{
   const HB_PCINFO * pInfo = &hb_pcInfo[ pCode[ 0 ] ];

   switch( pInfo->kind )
   {
      case HB_PCK_FIXED:
         return pInfo->nLen;
      case HB_PCK_STR1:
         /* opcode (1) + length-byte (1) + length bytes of data */
         return ( HB_SIZE ) 2 + pCode[ 1 ];
      case HB_PCK_STR2:
         /* opcode (1) + 2-byte length (2) + length bytes of data */
         return ( HB_SIZE ) 3 + HB_PCODE_MKUSHORT( &pCode[ 1 ] );
      case HB_PCK_STR3:
         /* opcode (1) + 3-byte length (3) + length bytes of data */
         return ( HB_SIZE ) 4 + HB_PCODE_MKUINT24( &pCode[ 1 ] );
      case HB_PCK_SWITCH:
      {
         /* [opcode][caseCount:2][N entries]; each entry = literal + jump */
         HB_USHORT       count = HB_PCODE_MKUSHORT( &pCode[ 1 ] );
         const HB_BYTE * p     = pCode + 3;
         HB_USHORT       k;
         for( k = 0; k < count; ++k )
         {
            switch( p[ 0 ] )          /* literal-push opcode */
            {
               case HB_P_PUSHLONG:      p += 5;             break;
               case HB_P_PUSHSTRSHORT:  p += 2 + p[ 1 ];    break;
               case HB_P_PUSHNIL:       p += 1;             break;
               default:                 return 0;  /* malformed */
            }
            switch( p[ 0 ] )          /* jump opcode */
            {
               case HB_P_JUMPNEAR:      p += 2;  break;
               case HB_P_JUMP:          p += 3;  break;
               case HB_P_JUMPFAR:       p += 4;  break;
               default:                 return 0;  /* malformed */
            }
         }
         return ( HB_SIZE ) ( p - pCode );
      }
      case HB_PCK_VARBLOCK:
         /* HB_P_PUSHBLOCKSHORT  (90): 1-byte total-size operand
          * HB_P_PUSHBLOCK       (89): 2-byte total-size operand
          * HB_P_PUSHBLOCKLARGE  (161): 3-byte total-size operand
          * HB_P_MPUSHBLOCK      (59): 2-byte total-size operand
          * HB_P_MPUSHBLOCKLARGE (159): 3-byte total-size operand
          * The operand IS the total instruction size (including opcode byte). */
         if( pCode[ 0 ] == HB_P_PUSHBLOCKSHORT )
            return ( HB_SIZE ) pCode[ 1 ];
         if( pCode[ 0 ] == HB_P_PUSHBLOCKLARGE ||
             pCode[ 0 ] == HB_P_MPUSHBLOCKLARGE )
            return ( HB_SIZE ) HB_PCODE_MKUINT24( &pCode[ 1 ] );
         /* HB_P_PUSHBLOCK, HB_P_MPUSHBLOCK */
         return ( HB_SIZE ) HB_PCODE_MKUSHORT( &pCode[ 1 ] );
      default:
         return 0;   /* HB_PCK_UNKNOWN — forces whole-function fallback */
   }
}

/* Signed jump displacement relative to the START of the jump instruction.
 * (hb_vmExecute does `pCode += disp` while pCode still points at the opcode
 * byte — verified against src/vm/hvm.c HB_P_JUMP* cases.) */
HB_ISIZ hb_pcodeJumpOffset( const HB_BYTE * pCode )
{
   switch( pCode[ 0 ] )
   {
      case HB_P_JUMPNEAR:
      case HB_P_JUMPFALSENEAR:
      case HB_P_JUMPTRUENEAR:
         return ( signed char ) pCode[ 1 ];
      case HB_P_JUMP:
      case HB_P_JUMPFALSE:
      case HB_P_JUMPTRUE:
         return HB_PCODE_MKSHORT( &pCode[ 1 ] );
      case HB_P_JUMPFAR:
      case HB_P_JUMPFALSEFAR:
      case HB_P_JUMPTRUEFAR:
         return HB_PCODE_MKINT24( &pCode[ 1 ] );
      default:
         return 0;
   }
}

static HB_BOOL hb_pcodeIsJump( HB_BYTE op )
{
   switch( op )
   {
      case HB_P_JUMP:      case HB_P_JUMPNEAR:      case HB_P_JUMPFAR:
      case HB_P_JUMPFALSE: case HB_P_JUMPFALSENEAR: case HB_P_JUMPFALSEFAR:
      case HB_P_JUMPTRUE:  case HB_P_JUMPTRUENEAR:  case HB_P_JUMPTRUEFAR:
         return HB_TRUE;
      default:
         return HB_FALSE;
   }
}

/* Scan pcode[0..nSize); populate *pMap.
 * Marks block leaders at: offset 0, every jump target, and the fall-through
 * instruction after every conditional jump.
 * Returns HB_FALSE if the stream is malformed (unknown opcode length, or a
 * jump target outside the buffer). The caller must free pMap->abLeader with
 * hb_xfree() on success. */
HB_BOOL hb_pcodeAnalyze( const HB_BYTE * pCode, HB_SIZE nSize, HB_PCMAP * pMap )
{
   HB_SIZE pos;

   pMap->nSize         = nSize;
   pMap->fAllSupported = HB_TRUE;
   pMap->abLeader      = ( HB_BYTE * ) hb_xgrabz( nSize + 1 );
   if( nSize > 0 )
      pMap->abLeader[ 0 ] = HB_TRUE;    /* entry is always a block leader */

   pos = 0;
   while( pos < nSize )
   {
      HB_BYTE op  = pCode[ pos ];
      HB_SIZE len = hb_pcodeInstrLen( &pCode[ pos ] );

      if( len == 0 || pos + len > nSize )   /* malformed or unmodelled opcode */
      {
         hb_xfree( pMap->abLeader );
         pMap->abLeader = NULL;
         return HB_FALSE;
      }

      if( ! hb_pcInfo[ op ].fSupported )
         pMap->fAllSupported = HB_FALSE;

      if( hb_pcodeIsJump( op ) )
      {
         HB_ISIZ disp   = hb_pcodeJumpOffset( &pCode[ pos ] );
         HB_ISIZ target = ( HB_ISIZ ) pos + disp;   /* relative to instruction start */

         if( target < 0 || ( HB_SIZE ) target > nSize )
         {
            hb_xfree( pMap->abLeader );
            pMap->abLeader = NULL;
            return HB_FALSE;
         }
         if( ( HB_SIZE ) target < nSize )
            pMap->abLeader[ target ] = HB_TRUE;      /* jump target is a leader */
         if( pos + len < nSize )
            pMap->abLeader[ pos + len ] = HB_TRUE;   /* fall-through is a leader */
      }
      pos += len;
   }
   return HB_TRUE;
}
