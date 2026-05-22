/* pcode opcode decoder for the straight-line LLVM backend.
 *
 * Copyright 2026 Antonio Linares / HarbourLLVM contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#ifndef HB_PCDEC_H_
#define HB_PCDEC_H_

#include "hbpcode.h"
#include "hbdefs.h"

typedef enum
{
   HB_PCK_FIXED,     /* fixed-length: nLen is the whole instruction length  */
   HB_PCK_STR1,      /* 1-byte length prefix then string data               */
   HB_PCK_STR2,      /* 2-byte length prefix then string data               */
   HB_PCK_STR3,      /* 3-byte length prefix then string data               */
   HB_PCK_VARBLOCK,  /* HB_P_PUSHBLOCK family: size is the operand          */
   HB_PCK_SWITCH,    /* HB_P_SWITCH: header + variable case table            */
   HB_PCK_UNKNOWN    /* not modelled — forces whole-function fallback        */
} HB_PCKIND;

typedef struct
{
   HB_PCKIND kind;       /* how to compute the instruction length               */
   HB_USHORT nLen;       /* for HB_PCK_FIXED: total bytes incl. the opcode byte */
   HB_BOOL   fSupported; /* HB_TRUE if the straight-line emitter handles it      */
} HB_PCINFO;

/* One entry per opcode value 0..HB_P_LAST_PCODE-1. */
extern const HB_PCINFO hb_pcInfo[];

/* Total byte length of the instruction at pCode (handles variable-length
 * string/block opcodes). Returns 0 if the opcode is HB_PCK_UNKNOWN. */
extern HB_SIZE hb_pcodeInstrLen( const HB_BYTE * pCode );

/* Per-function basic-block map. After hb_pcodeAnalyze() succeeds,
 * abLeader[off] is HB_TRUE when a basic block begins at pcode offset off.
 * fAllSupported is HB_TRUE only if every opcode in the function is in the
 * straight-line subset (genllvm.c uses the interpreter fallback otherwise). */
typedef struct
{
   HB_BYTE * abLeader;       /* nSize bytes, 1 = block leader              */
   HB_SIZE   nSize;          /* pcode length                               */
   HB_BOOL   fAllSupported;  /* HB_FALSE -> caller must use the fallback    */
} HB_PCMAP;

/* Scan pcode[0..nSize); fill *pMap. Returns HB_TRUE on a clean scan
 * (instruction boundaries consistent), HB_FALSE on a malformed stream.
 * The caller frees pMap->abLeader with hb_xfree(). */
extern HB_BOOL hb_pcodeAnalyze( const HB_BYTE * pCode, HB_SIZE nSize,
                                HB_PCMAP * pMap );

/* Signed jump displacement of the jump instruction at pCode (relative to
 * the START of that instruction). Valid only for jump opcodes. */
extern HB_ISIZ hb_pcodeJumpOffset( const HB_BYTE * pCode );

#endif /* HB_PCDEC_H_ */
