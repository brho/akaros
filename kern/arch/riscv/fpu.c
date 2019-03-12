#include <arch/softfloat.h>
#include <smp.h>
#include <trap.h>
#include <umem.h>

static uint32_t ls(uint64_t *addr)
{
	uint32_t r;
	asm("fld f0, %1; mftx.s %0, f0" : "=r"(r) : "m"(*addr));
	return r;
}

static void ss(uint64_t *addr, uint32_t val)
{
	asm("mxtf.s f0, %0; fsd f0, %1" : : "r"(val), "m"(*addr));
}

static int emulate_fpu_silly(struct hw_trapframe *state,
                             ancillary_state_t *silly)
{
	int insn;
	if (memcpy_from_user(current, &insn, (void *)state->epc, 4)) {
		state->cause = CAUSE_FAULT_FETCH;
		handle_trap(state);
	}

#define DECLARE_INSN(name, match, mask) bool is_##name = (insn & mask) == match;
#include <arch/opcodes.h>
#undef DECLARE_INSN

	int rd = (insn >> 27) & 0x1f;
	int rs1 = (insn >> 22) & 0x1f;
	int rs2 = (insn >> 17) & 0x1f;
	int rs3 = (insn >> 12) & 0x1f;

	int imm = (insn << 10) >> 20;
	int bimm = ((insn >> 10) & 0x7f) | ((insn & 0xf8000000) >> 20);

	void *load_address = (void *)(state->gpr[rs1] + imm);
	void *store_address = (void *)(state->gpr[rs1] + bimm);

	softfloat_t sf;
	sf.float_rounding_mode = silly->fsr >> 5;
	sf.float_exception_flags = silly->fsr & 0x1f;

	if (is_fsqrt_s)
		ss(&silly->fpr[rd], float32_sqrt(&sf, ls(&silly->fpr[rs1])));
	else if (is_fsqrt_d)
		silly->fpr[rd] = float64_sqrt(&sf, silly->fpr[rs1]);
	else if (is_fdiv_s)
		ss(&silly->fpr[rd], float32_div(&sf, ls(&silly->fpr[rs1]),
		                                ls(&silly->fpr[rs2])));
	else if (is_fdiv_d)
		silly->fpr[rd] =
		    float64_div(&sf, silly->fpr[rs1], silly->fpr[rs2]);
	/* Eventually, we will emulate the full FPU, including the below insns
	else if (is_mffsr)
	{
	        // use sf instead of silly->fsr
	        state->gpr[rd] = silly->fsr;
	}
	else if (is_mtfsr)
	{
	        // use sf instead of silly->fsr
	        int temp = silly->fsr;
	        silly->fsr = state->gpr[rs1] & 0xFF;
	        state->gpr[rd] = silly->fsr;
	}
	else if (is_fld)
	{
	        uint64_t dest;
	        if (!memcpy_from_user(current, &dest, load_address,
	sizeof(dest)))
	        {
	                state->cause = CAUSE_FAULT_LOAD;
	                state->badvaddr = (long)load_address;
	                handle_trap(state);
	        }
	        silly->fpr[rd] = dest;
	}
	else if (is_flw)
	{
	        uint32_t dest;
	        if (!memcpy_from_user(current, &dest, load_address,
	sizeof(dest)))
	        {
	                state->cause = CAUSE_FAULT_LOAD;
	                state->badvaddr = (long)load_address;
	                handle_trap(state);
	        }
	        silly->fpr[rd] = dest;
	}
	else if (is_fsd)
	{
	        if (!memcpy_to_user(current, store_address, &silly->fpr[rs2],
	sizeof(uint64_t)))
	        {
	                state->cause = CAUSE_FAULT_STORE;
	                state->badvaddr = (long)store_address;
	                handle_trap(state);
	        }
	}
	else if (is_flw)
	{
	        if (!memcpy_to_user(current, store_address, &silly->fpr[rs2],
	sizeof(uint32_t)))
	        {
	                state->cause = CAUSE_FAULT_STORE;
	                state->badvaddr = (long)store_address;
	                handle_trap(state);
	        }
	}
	*/
	else
		return 1;

	silly->fsr = sf.float_rounding_mode << 5 | sf.float_exception_flags;
	return 0;
}

/* For now we can only emulate missing compute insns, not the whole FPU */
int emulate_fpu(struct hw_trapframe *state)
{
	if (!(state->sr & SR_EF)) {
		state->cause = CAUSE_FP_DISABLED;
		handle_trap(state);
	}

	ancillary_state_t fp_state;
	save_fp_state(&fp_state);
	int code = emulate_fpu_silly(state, &fp_state);

	restore_fp_state(&fp_state);
	return code;
}
