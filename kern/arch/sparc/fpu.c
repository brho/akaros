#include <arch/sparcfpu.h>
#include <arch/arch.h>
#include <arch/trap.h>
#include <umem.h>
#include <pmap.h>
#include <smp.h>

static inline uint32_t* effective_address(trapframe_t* state, uint32_t insn)
{
	uint32_t rs1 = state->gpr[(insn>>14)&0x1F];
	uint32_t rs2 = state->gpr[insn&0x1F];
	struct { signed int x:13; } s;
	int32_t imm = s.x = insn&0x1FFF;

	return (uint32_t*)((insn & 0x2000) ? rs1+imm : rs1+rs2);
}

void fp_access_exception(trapframe_t* state, void* addr)
{
	state->fault_status = 1;
	state->fault_addr = (uint32_t)addr;
	data_access_exception(state);
}

static inline uint32_t fp_load_word(trapframe_t* state, uint32_t* addr)
{
	uint32_t word;
	if((long)addr % sizeof(word))
		address_unaligned(state);
	if(memcpy_from_user(current,&word,addr,sizeof(word)))
		fp_access_exception(state,addr);
	return word;
}

static inline uint64_t fp_load_dword(trapframe_t* state, uint64_t* addr)
{
	uint64_t word;
	if((long)addr % sizeof(word))
		address_unaligned(state);
	if(memcpy_from_user(current,&word,addr,sizeof(word)))
		fp_access_exception(state,addr);
	return word;
}

static inline void fp_store_word(trapframe_t* state, uint32_t* addr, uint32_t word)
{
	if((long)addr % sizeof(word))
		address_unaligned(state);
	if(memcpy_to_user(current,addr,&word,sizeof(word)))
		fp_access_exception(state,addr);
}

static inline void fp_store_dword(trapframe_t* state, uint64_t* addr, uint64_t word)
{
	if((long)addr % sizeof(word))
		address_unaligned(state);
	if(memcpy_to_user(current,addr,&word,sizeof(word)))
		fp_access_exception(state,addr);
}

void emulate_fpu(trapframe_t* state, ancillary_state_t* astate)
{
	sparcfpu_t thefpu;
	sparcfpu_t* fpu = &thefpu;
	sparcfpu_init(fpu);

	// pretend there are no FP exceptions right now.
	// we should catch them again after emulation 
	sparcfpu_setFSR(fpu,astate->fsr & ~0x1C000);
	memcpy(fpu->freg,astate->fpr,sizeof(astate->fpr));

	int annul = 0;
	uint32_t nnpc = state->npc+4;

	uint64_t dword;
	uint32_t reg,word;
	int32_t disp;
	uint32_t* addr;
	struct { signed int x:22; } disp22;

	uint32_t insn = fp_load_word(state,(uint32_t*)state->pc);
	fp_insn_t* fpi = (fp_insn_t*)&insn;

	switch(insn >> 30)
	{
		case 0:
			switch((insn >> 22) & 0x7)
			{
				case 6:
				{
					int cond = (insn>>25)&0xF;
					if(check_fcc[cond][fpu->FSR.fcc])
					{
						annul = (cond == fccA || cond == fccN) && ((insn>>29)&1);
						disp = disp22.x = insn & 0x3FFFFF;
						nnpc = state->pc + 4*disp;
					}
					else annul = (insn>>29)&1;
					break;
				}
				default:
					illegal_instruction(state);
					break;
			}
			break;
		case 1:
			illegal_instruction(state);
			break;
		case 2:
			switch((insn >> 19) & 0x3F)
			{
				case 0x34:
					sparcfpu_fpop1(fpu,*fpi);
					break;
				case 0x35:
					sparcfpu_fpop2(fpu,*fpi);
					break;
				default:
					illegal_instruction(state);
					break;
			}
			break;
		case 3:
			switch((insn >> 19) & 0x3F)
			{
				case 0x20:
					sparcfpu_wrregs(fpu,(insn>>25)&0x1F,fp_load_word(state,effective_address(state,insn)));
					break;
				case 0x21: // ldfsr
					addr = effective_address(state,insn);
					word = fp_load_word(state,addr);
					sparcfpu_setFSR(fpu,word);
					break;
				case 0x23:
					addr = effective_address(state,insn);
					reg = (insn>>25)&0x1F;
					dword = fp_load_dword(state,(uint64_t*)addr);
					sparcfpu_wrregs(fpu,reg,(uint32_t)(dword>>32));
					sparcfpu_wrregs(fpu,reg+1,(uint32_t)dword);
					break;
				case 0x24:
					addr = effective_address(state,insn);
					fp_store_word(state,addr,sparcfpu_regs(fpu,(insn>>25)&0x1F));
					break;
				case 0x25: // stfsr
					addr = effective_address(state,insn);
					fp_store_word(state,addr,sparcfpu_getFSR(fpu));
					fpu->FSR.ftt = 0;
					break;
				case 0x27:
					addr = effective_address(state,insn);
					reg = (insn>>25)&0x1F;
					dword = (((uint64_t)sparcfpu_regs(fpu,reg))<<32)|(uint64_t)sparcfpu_regs(fpu,reg+1);
					fp_store_dword(state,(uint64_t*)addr,dword);
					break;
				default:
					illegal_instruction(state);
					break;
			}
			break;
	}

	astate->fsr = sparcfpu_getFSR(fpu);

	// got an FP exception after re-execution
	if(fpu->FSR.ftt)
		real_fp_exception(state,astate);
	else
	{
		if(!annul)
		{
			state->pc = state->npc;
			state->npc = nnpc;
		}
		else
		{
			state->pc = nnpc;
			state->npc = nnpc+1;
		}
	}

	static_assert(sizeof(astate->fpr) == sizeof(fpu->freg));
	memcpy(astate->fpr,fpu->freg,sizeof(astate->fpr));
}
