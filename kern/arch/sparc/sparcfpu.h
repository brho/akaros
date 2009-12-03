/* Author: Andrew S. Waterman
 *         Parallel Computing Laboratory
 *         Electrical Engineering and Computer Sciences
 *         University of California, Berkeley
 *
 * Copyright (c) 2008, The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University of California, Berkeley nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS ''AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE REGENTS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef _SPARCFPU_H
#define _SPARCFPU_H

#ifdef __cplusplus
  extern "C" {
#endif

#include <arch/arch.h>
#include <arch/softfloat.h>

typedef struct
{
#ifdef BIG_ENDIAN
  uint32_t rd : 2;
  uint32_t unused : 2;
  uint32_t TEM : 5;
  uint32_t NS : 1;
  uint32_t res : 2;
  uint32_t ver : 3;
  uint32_t ftt : 3;
  uint32_t qne : 1;
  uint32_t unused2 : 1;
  uint32_t fcc : 2;
  uint32_t aexc : 5;
  uint32_t cexc : 5;
#else
  uint32_t cexc : 5;
  uint32_t aexc : 5;
  uint32_t fcc : 2;
  uint32_t unused2 : 1;
  uint32_t qne : 1;
  uint32_t ftt : 3;
  uint32_t ver : 3;
  uint32_t res : 2;
  uint32_t NS : 1;
  uint32_t TEM : 5;
  uint32_t unused : 2;
  uint32_t rd : 2;
#endif
} fsr_t;

typedef struct
{
#ifdef BIG_ENDIAN
  uint32_t op : 2;
  uint32_t rd : 5;
  uint32_t op3 : 6;
  uint32_t rs1 : 5;
  uint32_t opf : 9;
  uint32_t rs2 : 5;
#else
  uint32_t rs2 : 5;
  uint32_t opf : 9;
  uint32_t rs1 : 5;
  uint32_t op3 : 6;
  uint32_t rd : 5;
  uint32_t op : 2;
#endif
} fp_insn_t;

typedef struct
{
  uint64_t dummy; /* force 8-byte alignment*/
  uint32_t freg[32];
  fsr_t FSR;
  softfloat_t softfloat;
} sparcfpu_t;

enum FPop1opcodes
{
  opFMOVs=0x01,
  opFNEGs=0x05,
  opFABSs=0x09,
  opFSQRTs=0x29,
  opFSQRTd=0x2A,
  opFSQRTq=0x2B,
  opFADDs=0x41,
  opFADDd=0x42,
  opFADDq=0x43,
  opFSUBs=0x45,
  opFSUBd=0x46,
  opFSUBq=0x47,
  opFMULs=0x49,
  opFMULd=0x4A,
  opFMULq=0x4B,
  opFDIVs=0x4D,
  opFDIVd=0x4E,
  opFDIVq=0x4F,
  opFsMULd=0x69,
  opFdMULq=0x6E,
  opFiTOs=0xC4,
  opFdTOs=0xC6,
  opFqTOs=0xC7,
  opFiTOd=0xC8,
  opFsTOd=0xC9,
  opFqTOd=0xCB,
  opFiTOq=0xCC,
  opFsTOq=0xCD,
  opFdTOq=0xCE,
  opFsTOi=0xD1,
  opFdTOi=0xD2,
  opFqTOi=0xD3
};

enum FPop2opcodes
{
  opFCMPs=0x51,
  opFCMPd=0x52,
  opFCMPq=0x53,
  opFCMPEs=0x55,
  opFCMPEd=0x56,
  opFCMPEq=0x57
};

enum fp_traps
{
  fp_trap_none,
  fp_trap_IEEE_754_exception,
  fp_trap_unfinished_FPop,
  fp_trap_unimplemented_FPop,
  fp_trap_sequence_error,
  fp_trap_hardware_error,
  fp_trap_invalid_fp_register,
  fp_trap_reserved
};

enum ieee_754_exceptions
{
  ieee_754_exception_inexact=1,
  ieee_754_exception_division_by_zero=2,
  ieee_754_exception_underflow=4,
  ieee_754_exception_overflow=8,
  ieee_754_exception_invalid=16,
};

enum fconds
{
  fccN,fccNE,fccLG,fccUL,fccL,fccUG,fccG,fccU,
  fccA,fccE,fccUE,fccGE,fccUGE,fccLE,fccULE,fccO
};

// check if a branch is taken based upon condition (outer dimension)
// and current FP condition code value (inner dimension)
static const uint8_t check_fcc[16][4] = {
  {0,0,0,0},
  {0,1,1,1},
  {0,1,1,0},
  {0,1,0,1},
  {0,1,0,0},
  {0,0,1,1},
  {0,0,1,0},
  {0,0,0,1},
  {1,1,1,1},
  {1,0,0,0},
  {1,0,0,1},
  {1,0,1,0},
  {1,0,1,1},
  {1,1,0,0},
  {1,1,0,1},
  {1,1,1,0}
};

void sparcfpu_init(sparcfpu_t* fpu);

void sparcfpu_fpop1(sparcfpu_t* fpu, fp_insn_t insn);
void sparcfpu_fpop2(sparcfpu_t* fpu, fp_insn_t insn);
void sparcfpu_setFSR(sparcfpu_t* fpu, uint32_t newFSR); 
uint32_t sparcfpu_getFSR(sparcfpu_t* fpu); 

void sparcfpu_wrregs(sparcfpu_t* fpu, uint32_t reg, float32 val);
void sparcfpu_wrregd(sparcfpu_t* fpu, uint32_t reg, float64 val);
void sparcfpu_wrregq(sparcfpu_t* fpu, uint32_t reg, float128 val);
float32 sparcfpu_regs(sparcfpu_t* fpu, uint32_t reg);
float64 sparcfpu_regd(sparcfpu_t* fpu, uint32_t reg);
float128 sparcfpu_regq(sparcfpu_t* fpu, uint32_t reg);

#ifdef __cplusplus
  }
#endif

#endif

