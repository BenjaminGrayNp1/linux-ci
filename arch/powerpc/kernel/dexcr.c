#include <linux/cpu.h>
#include <linux/irqflags.h>
#include <linux/percpu.h>
#include <linux/prctl.h>
#include <linux/sysctl.h>
#include <linux/types.h>
#include <linux/capability.h>

/* XXX all of these defines maybe don't belong here */
#define DEXCR_PROn_SH(aspect)	(63 - (32 + (aspect))) /* Problem State (Userspace) */
#define DEXCR_ASPECT_SBHE	0 /* Speculative Branch Hint Enable */

DEFINE_PER_CPU(u64, cpu_dexcr);

static int spec_branch_hint_enable = -1;

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

	tsk->thread.dexcr_ovrd &= ~mask;
	tsk->thread.dexcr_ovrd |= data;
	tsk->thread.dexcr_mask |= mask;

	update_dexcr_on_cpu(NULL);

	return 0;
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
