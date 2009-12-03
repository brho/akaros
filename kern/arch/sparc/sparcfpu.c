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


#include <arch/sparcfpu.h>
#include <assert.h>
#include <string.h>

void sparcfpu_init(sparcfpu_t* fpu)
{
  memset(fpu->freg,0,sizeof(fpu->freg));
  memset(&fpu->FSR,0,sizeof(fpu->FSR));
  static_assert(sizeof(fpu->FSR) == 4);
  softfloat_init(&fpu->softfloat);
}

void sparcfpu_setFSR(sparcfpu_t* fpu, uint32_t newFSR)
{
  fpu->FSR.rd = newFSR >> 30;
  fpu->FSR.TEM = newFSR >> 23;
  fpu->FSR.ftt = newFSR >> 14;
  fpu->FSR.aexc = newFSR >> 5;
  fpu->FSR.cexc = newFSR;
}

uint32_t sparcfpu_getFSR(sparcfpu_t* fpu)
{
  return fpu->FSR.rd   << 30 |
         fpu->FSR.TEM  << 23 |
         fpu->FSR.ftt  << 14 |
         fpu->FSR.aexc <<  5 |
         fpu->FSR.cexc;
}

#define fpop1_trap_if(cond,code) do { if(cond) { fpu->FSR.ftt = (code); goto fpop1_done; } } while(0)
#define fpop2_trap_if(cond,code) do { if(cond) { fpu->FSR.ftt = (code); goto fpop2_done; } } while(0)

#define handle_exceptions(fpop,allowed) do { \
  assert(!(fpu->softfloat.float_exception_flags & ~(allowed))); \
  if(fpu->FSR.TEM & fpu->softfloat.float_exception_flags) \
  { \
    fpu->FSR.cexc = fpu->softfloat.float_exception_flags; \
    fpop##_trap_if(1,fp_trap_IEEE_754_exception); \
  } \
  fpu->FSR.aexc |= fpu->softfloat.float_exception_flags; \
  } while(0)

#define fpop1_handle_exceptions(allowed) handle_exceptions(fpop1,allowed)
#define fpop2_handle_exceptions(allowed) handle_exceptions(fpop2,allowed)

#define fpop1_check_double_align(reg) fpop1_trap_if((reg)&1,fp_trap_invalid_fp_register)
#define fpop2_check_double_align(reg) fpop2_trap_if((reg)&1,fp_trap_invalid_fp_register)
#define fpop1_check_quad_align(reg) fpop1_trap_if((reg)&3,fp_trap_invalid_fp_register)
#define fpop2_check_quad_align(reg) fpop2_trap_if((reg)&3,fp_trap_invalid_fp_register)

void sparcfpu_fpop1(sparcfpu_t* fpu, fp_insn_t insn)
{
  fpu->FSR.ftt = 0;
  fpu->softfloat.float_exception_flags = 0;
  fpu->softfloat.float_rounding_mode = fpu->FSR.rd;

  switch(insn.opf)
  {
    case opFiTOs: {
      float32 f = int32_to_float32(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact);
      sparcfpu_wrregs(fpu,insn.rd,f);
      break;
    }
    case opFiTOd: {
      fpop1_check_double_align(insn.rd);
      float64 f = int32_to_float64(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs2));
      fpop1_handle_exceptions(0);
      sparcfpu_wrregd(fpu,insn.rd,f);
      break;
    }
    case opFiTOq: {
      fpop1_check_quad_align(insn.rd);
      float128 f = int32_to_float128(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs2));
      fpop1_handle_exceptions(0);
      sparcfpu_wrregq(fpu,insn.rd,f);
      break;
    }
    case opFsTOi: {
      fpu->softfloat.float_rounding_mode = float_round_to_zero;
      int32_t i = float32_to_int32(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_invalid);
      sparcfpu_wrregs(fpu,insn.rd,i);
      break;
    }
    case opFdTOi: {
      fpu->softfloat.float_rounding_mode = float_round_to_zero;
      fpop1_check_double_align(insn.rs2);
      int32_t i = float64_to_int32(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_invalid);
      sparcfpu_wrregs(fpu,insn.rd,i);
      break;
    }
    case opFqTOi: {
      fpu->softfloat.float_rounding_mode = float_round_to_zero;
      fpop1_check_quad_align(insn.rs2);
      int32_t i = float128_to_int32(&fpu->softfloat,sparcfpu_regq(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_invalid);
      sparcfpu_wrregs(fpu,insn.rd,i);
      break;
    }
    case opFsTOd: {
      fpop1_check_double_align(insn.rd);
      float64 f = float32_to_float64(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_invalid);
      sparcfpu_wrregd(fpu,insn.rd,f);
      break;
    }
    case opFsTOq: {
      fpop1_check_quad_align(insn.rd);
      float128 f = float32_to_float128(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregq(fpu,insn.rd,f);
      break;
    }
    case opFdTOs: {
      fpop1_check_double_align(insn.rs2);
      float32 f = float64_to_float32(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregs(fpu,insn.rd,f);
      break;
    }
    case opFdTOq: {
      fpop1_check_double_align(insn.rs2);
      fpop1_check_quad_align(insn.rd);
      float128 f = float64_to_float128(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_invalid);
      sparcfpu_wrregq(fpu,insn.rd,f);
      break;
    }
    case opFqTOs: {
      fpop1_check_quad_align(insn.rs2);
      float32 f = float128_to_float32(&fpu->softfloat,sparcfpu_regq(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregs(fpu,insn.rd,f);
      break;
    }
    case opFqTOd: {
      fpop1_check_quad_align(insn.rs2);
      fpop1_check_double_align(insn.rd);
      float64 f = float128_to_float64(&fpu->softfloat,sparcfpu_regq(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregd(fpu,insn.rd,f);
      break;
    }
    case opFMOVs:
      sparcfpu_wrregs(fpu,insn.rd,sparcfpu_regs(fpu,insn.rs2));
      break;
    case opFNEGs:
      sparcfpu_wrregs(fpu,insn.rd,sparcfpu_regs(fpu,insn.rs2) ^ 0x80000000);
      break;
    case opFABSs:
      sparcfpu_wrregs(fpu,insn.rd,sparcfpu_regs(fpu,insn.rs2) &~0x80000000);
      break;
    case opFSQRTs: {
      float32 f = float32_sqrt(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_invalid);
      sparcfpu_wrregs(fpu,insn.rd,f);
      break;
    }
    case opFSQRTd: {
      fpop1_check_double_align(insn.rs2);      
      fpop1_check_double_align(insn.rd);      
      float64 f = float64_sqrt(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_invalid);
      sparcfpu_wrregd(fpu,insn.rd,f);
      break;
    }
    case opFSQRTq: {
      fpop1_check_quad_align(insn.rs2);      
      fpop1_check_quad_align(insn.rd);      
      float128 f = float128_sqrt(&fpu->softfloat,sparcfpu_regq(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_invalid);
      sparcfpu_wrregq(fpu,insn.rd,f);
      break;
    }
    case opFADDs: {
      float32 f = float32_add(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs1),sparcfpu_regs(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregs(fpu,insn.rd,f);
      break;
    }
    case opFADDd: {
      fpop1_check_double_align(insn.rs1);
      fpop1_check_double_align(insn.rs2);
      fpop1_check_double_align(insn.rd);      
      float64 f = float64_add(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs1),sparcfpu_regd(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregd(fpu,insn.rd,f);
      break;
    }
    case opFADDq: {
      fpop1_check_quad_align(insn.rs1);
      fpop1_check_quad_align(insn.rs2);
      fpop1_check_quad_align(insn.rd);      
      float128 f = float128_add(&fpu->softfloat,sparcfpu_regq(fpu,insn.rs1),sparcfpu_regq(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregq(fpu,insn.rd,f);
      break;
    }
    case opFSUBs: {
      float32 f = float32_sub(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs1),sparcfpu_regs(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregs(fpu,insn.rd,f);
      break;
    }
    case opFSUBd: {
      fpop1_check_double_align(insn.rs1);
      fpop1_check_double_align(insn.rs2);
      fpop1_check_double_align(insn.rd);      
      float64 f = float64_sub(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs1),sparcfpu_regd(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregd(fpu,insn.rd,f);
      break;
    }
    case opFSUBq: {
      fpop1_check_quad_align(insn.rs1);
      fpop1_check_quad_align(insn.rs2);
      fpop1_check_quad_align(insn.rd);      
      float128 f = float128_sub(&fpu->softfloat,sparcfpu_regq(fpu,insn.rs1),sparcfpu_regq(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregq(fpu,insn.rd,f);
      break;
    }
    case opFMULs: {
      float32 f = float32_mul(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs1),sparcfpu_regs(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregs(fpu,insn.rd,f);
      break;
    }
    case opFMULd: {
      fpop1_check_double_align(insn.rs1);
      fpop1_check_double_align(insn.rs2);
      fpop1_check_double_align(insn.rd);      
      float64 f = float64_mul(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs1),sparcfpu_regd(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregd(fpu,insn.rd,f);
      break;
    }
    case opFMULq: {
      fpop1_check_quad_align(insn.rs1);
      fpop1_check_quad_align(insn.rs2);
      fpop1_check_quad_align(insn.rd);      
      float128 f = float128_mul(&fpu->softfloat,sparcfpu_regq(fpu,insn.rs1),sparcfpu_regq(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregq(fpu,insn.rd,f);
      break;
    }
    case opFsMULd: {
      fpop1_check_double_align(insn.rd);      
      float64 f = float64_mul(&fpu->softfloat,float32_to_float64(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs1)),float32_to_float64(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs2)));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregd(fpu,insn.rd,f);
      break;
    }
    case opFdMULq: {
      fpop1_check_double_align(insn.rs1);
      fpop1_check_double_align(insn.rs2);
      fpop1_check_quad_align(insn.rd);      
      float128 f = float128_mul(&fpu->softfloat,float64_to_float128(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs1)),float64_to_float128(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs2)));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregq(fpu,insn.rd,f);
      break;
    }
    case opFDIVs: {
      float32 f = float32_div(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs1),sparcfpu_regs(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_divbyzero | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregs(fpu,insn.rd,f);
      break;
    }
    case opFDIVd: {
      fpop1_check_double_align(insn.rs1);
      fpop1_check_double_align(insn.rs2);
      fpop1_check_double_align(insn.rd);      
      float64 f = float64_div(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs1),sparcfpu_regd(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_divbyzero | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregd(fpu,insn.rd,f);
      break;
    }
    case opFDIVq: {
      fpop1_check_quad_align(insn.rs1);
      fpop1_check_quad_align(insn.rs2);
      fpop1_check_quad_align(insn.rd);      
      float128 f = float128_div(&fpu->softfloat,sparcfpu_regq(fpu,insn.rs1),sparcfpu_regq(fpu,insn.rs2));
      fpop1_handle_exceptions(float_flag_inexact | float_flag_divbyzero | float_flag_overflow | float_flag_underflow | float_flag_invalid);
      sparcfpu_wrregq(fpu,insn.rd,f);
      break;
    }
  }

  fpop1_done:
  ;
}

void sparcfpu_fpop2(sparcfpu_t* fpu, fp_insn_t insn)
{
  fpu->FSR.ftt = 0;
  fpu->softfloat.float_exception_flags = 0;
  fpu->softfloat.float_rounding_mode = fpu->FSR.rd;

  switch(insn.opf)
  {
    case opFCMPs: {
      int eq = float32_eq(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs1),sparcfpu_regs(fpu,insn.rs2));
      fpop2_handle_exceptions(float_flag_invalid);
      int lt = float32_lt(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs1),sparcfpu_regs(fpu,insn.rs2));
      int gt = float32_lt(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs2),sparcfpu_regs(fpu,insn.rs1));
      assert(eq+lt+gt <= 1);
      fpu->FSR.fcc = eq ? 0 : (lt ? 1 : (gt ? 2 : 3));
      break;
    }
    case opFCMPEs: {
      int eq = float32_eq_signaling(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs1),sparcfpu_regs(fpu,insn.rs2));
      fpop2_handle_exceptions(float_flag_invalid);
      int lt = float32_lt(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs1),sparcfpu_regs(fpu,insn.rs2));
      int gt = float32_lt(&fpu->softfloat,sparcfpu_regs(fpu,insn.rs2),sparcfpu_regs(fpu,insn.rs1));
      assert(eq+lt+gt <= 1);
      fpu->FSR.fcc = eq ? 0 : (lt ? 1 : (gt ? 2 : 3));
      break;
    }
    case opFCMPd: {
      fpop2_check_double_align(insn.rs1);
      fpop2_check_double_align(insn.rs2);
      int eq = float64_eq(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs1),sparcfpu_regd(fpu,insn.rs2));
      fpop2_handle_exceptions(float_flag_invalid);
      int lt = float64_lt(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs1),sparcfpu_regd(fpu,insn.rs2));
      int gt = float64_lt(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs2),sparcfpu_regd(fpu,insn.rs1));
      assert(eq+lt+gt <= 1);
      fpu->FSR.fcc = eq ? 0 : (lt ? 1 : (gt ? 2 : 3));
      break;
    }
    case opFCMPEd: {
      fpop2_check_double_align(insn.rs1);
      fpop2_check_double_align(insn.rs2);
      int eq = float64_eq_signaling(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs1),sparcfpu_regd(fpu,insn.rs2));
      fpop2_handle_exceptions(float_flag_invalid);
      int lt = float64_lt(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs1),sparcfpu_regd(fpu,insn.rs2));
      int gt = float64_lt(&fpu->softfloat,sparcfpu_regd(fpu,insn.rs2),sparcfpu_regd(fpu,insn.rs1));
      assert(eq+lt+gt <= 1);
      fpu->FSR.fcc = eq ? 0 : (lt ? 1 : (gt ? 2 : 3));
      break;
    }
    case opFCMPq: {
      fpop2_check_quad_align(insn.rs1);
      fpop2_check_quad_align(insn.rs2);
      int eq = float128_eq(&fpu->softfloat,sparcfpu_regq(fpu,insn.rs1),sparcfpu_regq(fpu,insn.rs2));
      fpop2_handle_exceptions(float_flag_invalid);
      int lt = float128_lt(&fpu->softfloat,sparcfpu_regq(fpu,insn.rs1),sparcfpu_regq(fpu,insn.rs2));
      int gt = float128_lt(&fpu->softfloat,sparcfpu_regq(fpu,insn.rs2),sparcfpu_regq(fpu,insn.rs1));
      assert(eq+lt+gt <= 1);
      fpu->FSR.fcc = eq ? 0 : (lt ? 1 : (gt ? 2 : 3));
      break;
    }
    case opFCMPEq: {
      fpop2_check_quad_align(insn.rs1);
      fpop2_check_quad_align(insn.rs2);
      int eq = float128_eq_signaling(&fpu->softfloat,sparcfpu_regq(fpu,insn.rs1),sparcfpu_regq(fpu,insn.rs2));
      fpop2_handle_exceptions(float_flag_invalid);
      int lt = float128_lt(&fpu->softfloat,sparcfpu_regq(fpu,insn.rs1),sparcfpu_regq(fpu,insn.rs2));
      int gt = float128_lt(&fpu->softfloat,sparcfpu_regq(fpu,insn.rs2),sparcfpu_regq(fpu,insn.rs1));
      assert(eq+lt+gt <= 1);
      fpu->FSR.fcc = eq ? 0 : (lt ? 1 : (gt ? 2 : 3));
      break;
    }
  }

  fpop2_done:
  ;
}

void sparcfpu_wrregs(sparcfpu_t* fpu, uint32_t reg, float32 val)
{
  assert(reg < 32);
  fpu->freg[reg] = val;
}

void sparcfpu_wrregd(sparcfpu_t* fpu, uint32_t reg, float64 val)
{
  assert(reg < 32 && !(reg&1));
  sparcfpu_wrregs(fpu,reg,(uint32_t)(val>>32));
  sparcfpu_wrregs(fpu,reg+1,(uint32_t)val);
}

void sparcfpu_wrregq(sparcfpu_t* fpu, uint32_t reg, float128 val)
{
  assert(reg < 32 && !(reg&3));
  sparcfpu_wrregd(fpu,reg,val.high);
  sparcfpu_wrregd(fpu,reg+2,val.low);
}

float32 sparcfpu_regs(sparcfpu_t* fpu, uint32_t reg)
{
  assert(reg < 32);
  return fpu->freg[reg];
}

float64 sparcfpu_regd(sparcfpu_t* fpu, uint32_t reg)
{
  assert(reg < 32 && !(reg&1));
  return (((float64)sparcfpu_regs(fpu,reg))<<32) | (float64)sparcfpu_regs(fpu,reg+1);
}

float128 sparcfpu_regq(sparcfpu_t* fpu, uint32_t reg)
{
  assert(reg < 32 && !(reg&3));
  float128 f;
  f.high = sparcfpu_regd(fpu,reg);
  f.low = sparcfpu_regd(fpu,reg+2);
  return f;
}
