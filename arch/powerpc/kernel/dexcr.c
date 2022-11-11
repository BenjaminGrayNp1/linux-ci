#include <linux/cache.h>
#include <linux/capability.h>
#include <linux/init.h>
#include <linux/prctl.h>
#include <linux/sched.h>

#include <asm/cpu_has_feature.h>
#include <asm/cputable.h>
#include <asm/disassemble.h>
#include <asm/inst.h>
#include <asm/ppc-opcode.h>
#include <asm/processor.h>
#include <asm/reg.h>

#define DEFAULT_DEXCR	0

/* Allow process configuration of these by default */
#define DEXCR_PRCTL_EDITABLE (DEXCR_PRO_SBHE | DEXCR_PRO_IBRTPD | \
			      DEXCR_PRO_SRAPD | DEXCR_PRO_NPHIE)

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
	unsigned long dexcr = DEFAULT_DEXCR;

	/* Apply prctl overrides */
	dexcr = (dexcr & ~t->dexcr_mask) | t->dexcr_override;

	return dexcr;
}

static void update_dexcr_on_cpu(void *info)
{
	mtspr(SPRN_DEXCR, get_thread_dexcr(&current->thread));
}

static int dexcr_aspect_get(struct task_struct *task, unsigned int aspect)
{
	int ret = 0;

	if (aspect & DEXCR_PRCTL_EDITABLE)
		ret |= PR_PPC_DEXCR_PRCTL;

	if (aspect & task->thread.dexcr_mask) {
		if (aspect & task->thread.dexcr_override) {
			if (aspect & task->thread.dexcr_forced)
				ret |= PR_PPC_DEXCR_FORCE_SET_ASPECT;
			else
				ret |= PR_PPC_DEXCR_SET_ASPECT;
		} else {
			ret |= PR_PPC_DEXCR_CLEAR_ASPECT;
		}
	}

	return ret;
}

int dexcr_prctl_get(struct task_struct *task, unsigned long which)
{
	switch (which) {
	case PR_PPC_DEXCR_SBHE:
		if (!cpu_has_feature(CPU_FTR_DEXCR_SBHE))
			return -ENODEV;
		return dexcr_aspect_get(task, DEXCR_PRO_SBHE);
	case PR_PPC_DEXCR_IBRTPD:
		if (!cpu_has_feature(CPU_FTR_DEXCR_IBRTPD))
			return -ENODEV;
		return dexcr_aspect_get(task, DEXCR_PRO_IBRTPD);
	case PR_PPC_DEXCR_SRAPD:
		if (!cpu_has_feature(CPU_FTR_DEXCR_SRAPD))
			return -ENODEV;
		return dexcr_aspect_get(task, DEXCR_PRO_SRAPD);
	case PR_PPC_DEXCR_NPHIE:
		if (!cpu_has_feature(CPU_FTR_DEXCR_NPHIE))
			return -ENODEV;
		return dexcr_aspect_get(task, DEXCR_PRO_NPHIE);
	default:
		return -ENODEV;
	}
}

static int dexcr_aspect_set(struct task_struct *task, unsigned int aspect, unsigned long ctrl)
{
	if (!(aspect & DEXCR_PRCTL_EDITABLE))
		return -ENXIO;  /* Aspect is not allowed to be changed by prctl */

	if (aspect & task->thread.dexcr_forced)
		return -EPERM;  /* Aspect has been forced to current state */

	switch (ctrl) {
	case PR_PPC_DEXCR_SET_ASPECT:
		task->thread.dexcr_mask |= aspect;
		task->thread.dexcr_override |= aspect;
		break;
	case PR_PPC_DEXCR_FORCE_SET_ASPECT:
		task->thread.dexcr_mask |= aspect;
		task->thread.dexcr_override |= aspect;
		task->thread.dexcr_forced |= aspect;
		break;
	case PR_PPC_DEXCR_CLEAR_ASPECT:
		task->thread.dexcr_mask |= aspect;
		task->thread.dexcr_override &= ~aspect;
		break;
	default:
		return -ERANGE;
	}

	return 0;
}

int dexcr_prctl_set(struct task_struct *task, unsigned long which, unsigned long ctrl)
{
	int err = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (which) {
	case PR_PPC_DEXCR_SBHE:
		if (!cpu_has_feature(CPU_FTR_DEXCR_SBHE))
			return -ENODEV;
		err = dexcr_aspect_set(task, DEXCR_PRO_SBHE, ctrl);
		break;
	case PR_PPC_DEXCR_IBRTPD:
		if (!cpu_has_feature(CPU_FTR_DEXCR_IBRTPD))
			return -ENODEV;
		err = dexcr_aspect_set(task, DEXCR_PRO_IBRTPD, ctrl);
		break;
	case PR_PPC_DEXCR_SRAPD:
		if (!cpu_has_feature(CPU_FTR_DEXCR_SRAPD))
			return -ENODEV;
		err = dexcr_aspect_set(task, DEXCR_PRO_SRAPD, ctrl);
		break;
	case PR_PPC_DEXCR_NPHIE:
		if (!cpu_has_feature(CPU_FTR_DEXCR_NPHIE))
			return -ENODEV;
		err = dexcr_aspect_set(task, DEXCR_PRO_NPHIE, ctrl);
		break;
	default:
		return -ENODEV;
	}

	if (err)
		return err;

	update_dexcr_on_cpu(NULL);

	return 0;
}
