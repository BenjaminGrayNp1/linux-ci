#include <linux/irqflags.h>
#include <linux/percpu.h>
#include <linux/types.h>

DEFINE_PER_CPU(u64, cpu_dexcr);

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

	return dexcr;
}
