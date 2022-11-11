#include <linux/cache.h>
#include <linux/init.h>

#include <asm/cpu_has_feature.h>
#include <asm/cputable.h>
#include <asm/disassemble.h>
#include <asm/inst.h>
#include <asm/ppc-opcode.h>
#include <asm/processor.h>
#include <asm/reg.h>

#define DEFAULT_DEXCR	0

static int __init dexcr_init(void)
{
	if (!early_cpu_has_feature(CPU_FTR_ARCH_31))
		return 0;

	mtspr(SPRN_DEXCR, DEFAULT_DEXCR);

	return 0;
}
early_initcall(dexcr_init);

bool is_hashchk_trap(struct pt_regs const *regs)
{
	ppc_inst_t insn;

	if (!cpu_has_feature(CPU_FTR_DEXCR_NPHIE))
		return false;

	if (get_user_instr(insn, (void __user *)regs->nip)) {
		WARN_ON(1);
		return false;
	}

	if (ppc_inst_primary_opcode(insn) == 31 &&
	    get_xop(ppc_inst_val(insn)) == OP_31_XOP_HASHCHK)
		return true;

	return false;
}

unsigned long get_thread_dexcr(struct thread_struct const *t)
{
	return DEFAULT_DEXCR;
}
