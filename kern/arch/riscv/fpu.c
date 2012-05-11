#include <arch/trap.h>
#include <smp.h>
#include <umem.h>

int emulate_fpu(struct trapframe* state, ancillary_state_t* silly)
{
	int insn;
	if (!memcpy_from_user(current, &insn, (void*)state->epc, 4))
	{
		state->cause = CAUSE_FAULT_FETCH;
		handle_trap(state);
	}

	#define DECLARE_INSN(name, match, mask) bool is_##name = (insn & match) == mask;
	#include <arch/opcodes.h>
	#undef DECLARE_INSN

	int rd  = (insn >> 27) & 0x1f;
	int rs1 = (insn >> 22) & 0x1f;
	int rs2 = (insn >> 17) & 0x1f;
	int rs3 = (insn >> 12) & 0x1f;

	int imm = (insn << 10) >> 20;
	int bimm = ((insn >> 10) & 0x7f) | ((insn & 0xf8000000) >> 20);

	void* load_address = (void*)(state->gpr[rs1] + imm);
	void* store_address = (void*)(state->gpr[rs1] + bimm);

	if (is_mffsr)
	{
		state->gpr[rd] = silly->fsr;
	}
	else if (is_mtfsr)
	{
		silly->fsr = state->gpr[rs1];
	}
	else if (is_fld)
	{
		uint64_t dest;
		if (!memcpy_from_user(current, &dest, load_address, sizeof(dest)))
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
		if (!memcpy_from_user(current, &dest, load_address, sizeof(dest)))
		{
			state->cause = CAUSE_FAULT_LOAD;
			state->badvaddr = (long)load_address;
			handle_trap(state);
		}
		silly->fpr[rd] = dest;
	}
	else if (is_fsd)
	{
		if (!memcpy_to_user(current, store_address, &silly->fpr[rs2], sizeof(uint64_t)))
		{
			state->cause = CAUSE_FAULT_STORE;
			state->badvaddr = (long)store_address;
			handle_trap(state);
		}
	}
	else if (is_flw)
	{
		if (!memcpy_to_user(current, store_address, &silly->fpr[rs2], sizeof(uint32_t)))
		{
			state->cause = CAUSE_FAULT_STORE;
			state->badvaddr = (long)store_address;
			handle_trap(state);
		}
	}
	else
	  return 1;
	
	return 0;
}
