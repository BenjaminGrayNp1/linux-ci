#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/irqflags.h>
#include <linux/percpu.h>
#include <linux/prctl.h>
#include <linux/sysctl.h>
#include <linux/types.h>
#include <linux/capability.h>

#include <asm/disassemble.h>
#include <asm/inst.h>
#include <asm/ppc-opcode.h>

/* XXX all of these defines maybe don't belong here */
#define DEXCR_PROn_SH(aspect)	(63 - (32 + (aspect))) /* Problem State (Userspace) */
#define DEXCR_ASPECT_SBHE	0 /* Speculative Branch Hint Enable */
#define DEXCR_ASPECT_IBRTPD	3 /* Indirect Branch Recurrent Target Prediction Disable */
#define DEXCR_ASPECT_SRAPD	4 /* Subroutine Return Address Prediction Disable */
#define DEXCR_ASPECT_NPHIE	5 /* Non-Privileged Hash Instruction Enable */

DEFINE_PER_CPU(u64, cpu_dexcr);

static int spec_branch_hint_enable = -1;
static bool nonpriv_hash_insn_enable = IS_ENABLED(CONFIG_PPC_USER_ROP_PROTECT);

static int __init dexcr_init(void)
{
	int cpu;
	u64 dexcr;

	if (early_cpu_has_feature(CPU_FTR_ARCH_31)) {
		dexcr = mfspr(SPRN_DEXCR);
		for_each_possible_cpu(cpu)
			per_cpu(cpu_dexcr, cpu) = dexcr;
	}

	return 0;
}
early_initcall(dexcr_init);

u64 dexcr_thread_val(struct thread_struct const *t)
{
	u64 dexcr;

	dexcr = this_cpu_read(cpu_dexcr);

	/* Apply thread overrides */
	dexcr = (dexcr & ~t->dexcr_mask) | t->dexcr_ovrd;

	/* Apply system overrides */
	if (spec_branch_hint_enable != -1) {
		dexcr &= ~((u64)1 << DEXCR_PROn_SH(DEXCR_ASPECT_SBHE));
		dexcr |= ((u64)(spec_branch_hint_enable & 1))
				<< DEXCR_PROn_SH(DEXCR_ASPECT_SBHE);
	}

	if (cpu_has_feature(CPU_FTR_DEXCR_NPHIE)) {
		dexcr &= ~((u64)1 << DEXCR_PROn_SH(DEXCR_ASPECT_NPHIE));
		dexcr |= ((u64)nonpriv_hash_insn_enable)
				<< DEXCR_PROn_SH(DEXCR_ASPECT_NPHIE);
	}

	return dexcr;
}

static void update_dexcr_on_cpu(void *info)
{
	u64 dexcr;

	dexcr = dexcr_thread_val(&current->thread);
	mtspr(SPRN_DEXCR, dexcr);
}

/* Called from prctl interface: PR_PPC_SET_DEXCR_ASPECT */
int set_dexcr_aspect(struct task_struct *tsk, unsigned int asp, unsigned int val)
{
	u64 mask = 0;
	u64 data = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!cpu_has_feature(CPU_FTR_ARCH_31))
		return -EINVAL;

	switch (val) {
	case 0:
	case 1:
		break;
	default:
		return -EINVAL;
	}

	if (!(asp & PR_PPC_DEXCR_ASPECT_ALL) ||
	    (asp & ~PR_PPC_DEXCR_ASPECT_ALL))
		return -EINVAL;

	if (asp & PR_PPC_DEXCR_ASPECT_SBHE) {
		if (!cpu_has_feature(CPU_FTR_DEXCR_SBHE)) {
			pr_warn("DEXCR[SBHE] not implemented\n");
			return -EINVAL;
		}

		mask |= (u64)1 << DEXCR_PROn_SH(DEXCR_ASPECT_SBHE);
		data |= (u64)val << DEXCR_PROn_SH(DEXCR_ASPECT_SBHE);
	}

	if (asp & PR_PPC_DEXCR_ASPECT_SRAPD) {
		if (!cpu_has_feature(CPU_FTR_DEXCR_SRAPD)) {
			pr_warn("DEXCR[SRAPD] not implemented\n");
			return -EINVAL;
		}

		mask |= (u64)1 << DEXCR_PROn_SH(DEXCR_ASPECT_SRAPD);
		data |= (u64)val << DEXCR_PROn_SH(DEXCR_ASPECT_SRAPD);
	}

	if (asp & PR_PPC_DEXCR_ASPECT_IBRTPD) {
		if (!cpu_has_feature(CPU_FTR_DEXCR_IBRTPD)) {
			pr_warn("DEXCR[IBRTPD] not implemented\n");
			return -EINVAL;
		}

		mask |= (u64)1 << DEXCR_PROn_SH(DEXCR_ASPECT_IBRTPD);
		data |= (u64)val << DEXCR_PROn_SH(DEXCR_ASPECT_IBRTPD);
	}

	tsk->thread.dexcr_ovrd &= ~mask;
	tsk->thread.dexcr_ovrd |= data;
	tsk->thread.dexcr_mask |= mask;

	update_dexcr_on_cpu(NULL);

	return 0;
}

bool is_hashchk_trap(struct pt_regs const *regs)
{
	ppc_inst_t insn;

	/*
	 * XXX Just do a mfspr(SPRN_DEXCR) instead of checking
	 * nonpriv_hash_insn_enable?
	 */
	if (!cpu_has_feature(CPU_FTR_DEXCR_NPHIE) ||
		nonpriv_hash_insn_enable != 1)
		return false;

	if (get_user_instr(insn, (void __user *)regs->nip)) {
		WARN_ON(1);
		return false;
	}

	if (ppc_inst_primary_opcode(insn) == 31
		&& get_xop(ppc_inst_val(insn)) == OP_31_XOP_HASHCHK)
		return true;

	return false;
}

#ifdef CONFIG_SYSCTL

static const int min_sysctl_val = -1;

static int sysctl_dexcr_sbhe_handler(struct ctl_table *table, int write,
				     void *buf, size_t *lenp, loff_t *ppos)
{
	int err;
	int prev = spec_branch_hint_enable;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!cpu_has_feature(CPU_FTR_DEXCR_SBHE)) {
		pr_warn("DEXCR[SBHE] not implemented\n");
		return -EINVAL;
	}

	err = proc_dointvec_minmax(table, write, buf, lenp, ppos);
	if (err)
		return err;

	if (prev != spec_branch_hint_enable && write) {
		cpus_read_lock();
		on_each_cpu(update_dexcr_on_cpu, NULL, 1);
		cpus_read_unlock();
	}

	return 0;
}

static struct ctl_table dexcr_sbhe_ctl_table[] = {
	{
		.procname	= "speculative-branch-hint-enable",
		.data		= &spec_branch_hint_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= sysctl_dexcr_sbhe_handler,
		.extra1		= (void *)&min_sysctl_val,
		.extra2		= SYSCTL_ONE,
	},
	{}
};

static struct ctl_table dexcr_sbhe_ctl_root[] = {
	{
		.procname	= "kernel",
		.mode		= 0555,
		.child		= dexcr_sbhe_ctl_table,
	},
	{}
};

static int __init register_dexcr_aspects_sysctl(void)
{
	register_sysctl_table(dexcr_sbhe_ctl_root);
	return 0;
}
device_initcall(register_dexcr_aspects_sysctl);

#endif /* CONFIG_SYSCTL */

#ifdef CONFIG_DEBUG_FS

static int dexcr_nphie_set(void *data, u64 val)
{
	if (!CAP_SYS_ADMIN)
		return -EPERM;

	if (!cpu_has_feature(CPU_FTR_DEXCR_NPHIE)) {
		pr_warn("DEXCR[NPHIE] not implemented\n");
		return -EINVAL;
	}

	switch (val) {
	case 0:
	case 1:
		break;
	default:
		return -EINVAL;
	}

	if (!!val != nonpriv_hash_insn_enable) {
		nonpriv_hash_insn_enable = !!val;

		cpus_read_lock();
		on_each_cpu(update_dexcr_on_cpu, NULL, 1);
		cpus_read_unlock();
	}

	return 0;
}

static int dexcr_nphie_get(void *data, u64 *val)
{
	*val = (u64)nonpriv_hash_insn_enable;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_dexcr_nphie, dexcr_nphie_get, dexcr_nphie_set,
			 "%lld\n");

static __init int dexcr_nphie_debugfs_init(void)
{
	debugfs_create_file_unsafe("non-privileged-hash-insn-enable", 0600,
				   arch_debugfs_dir, NULL,
				   &fops_dexcr_nphie);
	return 0;
}
device_initcall(dexcr_nphie_debugfs_init);

#endif /* CONFIG_DEBUG_FS */
