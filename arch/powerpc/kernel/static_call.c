// SPDX-License-Identifier: GPL-2.0
#include <linux/bitops.h>
#include <linux/memory.h>
#include <linux/static_call.h>

#include <asm/code-patching.h>

static long sign_extend_long(unsigned long value, int index)
{
	if (sizeof(long) == 8)
		return sign_extend64(value, index);
	else
		return sign_extend32(value, index);
}

static void *ppc_function_toc(u32 *func)
{
	if (IS_ENABLED(CONFIG_PPC64_ELF_ABI_V2)) {
		/* There are two common global entry sequences we handle below
		 *
		 * 1. addis r2, r12, SI1
		 *    addi r2, SI2
		 *
		 * 2. lis r2, SI1
		 *    addi r2, SI2
		 *
		 * Where r12 contains the global entry point address (it is otherwise
		 * uninitialised, so doesn't matter what value we use if this is not
		 * a separate global entry point).
		 *
		 * Here we simulate running the given sequence and return the result it
		 * would calculate. If the sequence is not recognised we return NULL.
		 */
		u32 insn1 = *func;
		u32 insn2 = *(func + 1);
		unsigned long op_regs1 = insn1 & OP_RT_RA_MASK;
		unsigned long op_regs2 = insn2 & OP_RT_RA_MASK;
		unsigned long si1 = insn1 & OP_SI_MASK;
		unsigned long si2 = insn2 & OP_SI_MASK;
		unsigned long imm1 = sign_extend_long(si1 << 16, 31);
		unsigned long imm2 = sign_extend_long(si2, 15);
		unsigned long addr = 0;

		/* Simulate the first instruction */
		if (op_regs1 == ADDIS_R2_R12)
			addr += (unsigned long)func + imm1;
		else if (op_regs1 == LIS_R2)
			addr += imm1;
		else
			return NULL;

		/* Simulate the second instruction */
		if (op_regs2 == ADDI_R2_R2)
			addr += imm2;
		else
			return NULL;

		return (void *)addr;
	}

	return NULL;
}

static bool shares_toc(void *func1, void *func2)
{
	if (IS_ENABLED(CONFIG_PPC64_ELF_ABI_V2)) {
		void *func1_toc;
		void *func2_toc;

		if (func1 == NULL || func2 == NULL)
			return false;

		/* Assume the kernel only uses a single TOC */
		if (core_kernel_text((unsigned long)func1) &&
		    core_kernel_text((unsigned long)func2))
			return true;

		/* Fall back to calculating the TOC from common patterns
		 * if modules are involved
		 */
		func1_toc = ppc_function_toc(func1);
		func2_toc = ppc_function_toc(func2);
		return func1_toc != NULL && func2_toc != NULL && func1_toc == func2_toc;
	}

	return true;
}

static void *get_inst_addr(void *tramp)
{
	return tramp + (core_kernel_text((unsigned long)tramp)
				? PPC_SCT_INST_KERNEL
				: PPC_SCT_INST_MODULE);
}

static void *get_ret0_addr(void *tramp)
{
	return tramp + (core_kernel_text((unsigned long)tramp)
				? PPC_SCT_RET0_KERNEL
				: PPC_SCT_RET0_MODULE);
}

static void *get_data_addr(void *tramp)
{
	return tramp + (core_kernel_text((unsigned long) tramp)
				? PPC_SCT_DATA_KERNEL
				: PPC_SCT_DATA_MODULE);
}

void arch_static_call_transform(void *site, void *tramp, void *func, bool tail)
{
	int err;
	bool is_ret0 = (func == __static_call_return0);
	bool is_short;
	void *target = is_ret0 ? get_ret0_addr(tramp) : func;
	void *tramp_inst = get_inst_addr(tramp);

	if (!tramp)
		return;

	if (is_ret0)
		is_short = true;
	else if (shares_toc(tramp, target))
		is_short = is_offset_in_branch_range(
			(long)ppc_function_entry(target) - (long)tramp_inst);
	else
		/* Combine out-of-range with not sharing a TOC. Though it's possible
		 * an out-of-range target shares a TOC, handling this separately
		 * complicates the trampoline. It's simpler to always use the global
		 * entry point in this case.
		 */
		is_short = false;

	mutex_lock(&text_mutex);

	if (func && !is_short) {
		err = patch_ulong(get_data_addr(tramp), (unsigned long)target);
		if (err)
			goto out;
	}

	if (!func)
		err = patch_instruction(tramp_inst, ppc_inst(PPC_RAW_BLR()));
	else if (is_short)
		err = patch_branch(tramp_inst, ppc_function_entry(target), 0);
	else
		err = patch_instruction(tramp_inst, ppc_inst(PPC_RAW_NOP()));

out:
	mutex_unlock(&text_mutex);

	if (err)
		panic("%s: patching failed %pS at %pS\n", __func__, func, tramp);
}
EXPORT_SYMBOL_GPL(arch_static_call_transform);


#if IS_MODULE(CONFIG_PPC_STATIC_CALL_KUNIT_TEST)

#include "static_call_test.h"

int ppc_sc_kernel_target_1(struct kunit *test)
{
	toc_fixup(test);
	return 1;
}

int ppc_sc_kernel_target_2(struct kunit *test)
{
	toc_fixup(test);
	return 2;
}

DEFINE_STATIC_CALL(ppc_sc_kernel, ppc_sc_kernel_target_1);

int ppc_sc_kernel_call(struct kunit *test)
{
	return PROTECTED_SC(test, int, static_call(ppc_sc_kernel)(test));
}

int ppc_sc_kernel_call_indirect(struct kunit *test, int (*fn)(struct kunit *test))
{
	return PROTECTED_SC(test, int, fn(test));
}

long ppc_sc_kernel_target_big(struct kunit *test, long a, long b, long c, long d,
			      long e, long f, long g, long h, long i)
{
	toc_fixup(test);
	KUNIT_EXPECT_EQ(test, a, b);
	KUNIT_EXPECT_EQ(test, a, c);
	KUNIT_EXPECT_EQ(test, a, d);
	KUNIT_EXPECT_EQ(test, a, e);
	KUNIT_EXPECT_EQ(test, a, f);
	KUNIT_EXPECT_EQ(test, a, g);
	KUNIT_EXPECT_EQ(test, a, h);
	KUNIT_EXPECT_EQ(test, a, i);
	return ~a;
}

EXPORT_SYMBOL_GPL(ppc_sc_kernel_target_1);
EXPORT_SYMBOL_GPL(ppc_sc_kernel_target_2);
EXPORT_SYMBOL_GPL(ppc_sc_kernel_target_big);
EXPORT_STATIC_CALL_GPL(ppc_sc_kernel);
EXPORT_SYMBOL_GPL(ppc_sc_kernel_call);
EXPORT_SYMBOL_GPL(ppc_sc_kernel_call_indirect);

#endif /* IS_MODULE(CONFIG_PPC_STATIC_CALL_KUNIT_TEST) */
