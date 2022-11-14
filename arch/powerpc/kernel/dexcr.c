#include <linux/cache.h>
#include <linux/init.h>

#include <asm/cpu_has_feature.h>
#include <asm/cputable.h>
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

unsigned long get_thread_dexcr(struct thread_struct const *t)
{
	return DEFAULT_DEXCR;
}
