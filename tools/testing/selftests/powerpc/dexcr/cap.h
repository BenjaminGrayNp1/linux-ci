/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Simple capabilities getter/setter
 *
 * This header file contains helper functions and macros
 * required to get and set capabilities(7). Introduced so
 * we aren't the first to rely on libcap.
 */
#ifndef _SELFTESTS_POWERPC_DEXCR_CAP_H
#define _SELFTESTS_POWERPC_DEXCR_CAP_H

#include <stdbool.h>

bool cap_check_sysadmin(void);

void cap_drop_sysadmin(void);

#endif  /* _SELFTESTS_POWERPC_DEXCR_CAP_H */
