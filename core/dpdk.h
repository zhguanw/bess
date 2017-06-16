#ifndef BESS_DPDK_H_
#define BESS_DPDK_H_

#include <rte_version.h>

#define DPDK_VER_NUM(a, b, c) (((a << 16) | (b << 8) | (c)))

/* for DPDK 16.04 or newer */
#ifdef RTE_VER_YEAR
#define DPDK_VER DPDK_VER_NUM(RTE_VER_YEAR, RTE_VER_MONTH, RTE_VER_MINOR)
#endif

namespace bess {

bool IsDpdkInitialized();

// Safe to call multiple times.
void InitDpdk();

}  // namespace bess

#endif  // BESS_DPDK_H_
