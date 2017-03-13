#ifndef BESS_PACKET_H_
#define BESS_PACKET_H_

#include <cstdint>

#include <glog/logging.h>

namespace bess {

// For the physical address space   0x 000 0000 0000 - 0x fff 0000 0000 (16TB),
// we use the virtual address range 0x6000 0000 0000 - 0x6fff ffff ffff
const uintptr_t kVirtualAddressStart = 0x600000000000ull;
const uintptr_t kVirtualAddressEnd   = 0x700000000000ull;  // not inclusive

enum class Hugepage : size_t {
  kSize4KB = 1 << 12,  // normal pages
  kSize2MB = 1 << 21,
  kSize1GB = 1 << 30,
};

// Translate a virtual address of this process into a physical one.
// Unlike Virt2Phy(), the underlying page doesn't need to be a hugepage.
// (but still the pointer should be a valid one)
// Returns 0 if failed: invalid virtual address, no CAP_SYS_ADMIN, etc.
// This function is slow -- not meant to be used in the datapath.
uintptr_t Virt2PhyGeneric(void *ptr);

// Same as Virt2PhyGeneric(),
// but only valid for memory blocks allocated by AllocHugepage()
static inline uintptr_t Virt2Phy(void *ptr) {
	uintptr_t vaddr = reinterpret_cast<uintptr_t>(ptr);
	DCHECK(kVirtualAddressStart <= vaddr);
	DCHECK(vaddr < kVirtualAddressEnd);
	return vaddr ^ kVirtualAddressStart;
}

// Only valid for memory blocks allocated by AllocHugepage()
static inline void *Phy2Virt(uintptr_t paddr) {
	DCHECK(paddr < (kVirtualAddressEnd - kVirtualAddressStart));
	return reinterpret_cast<void *>(paddr + kVirtualAddressStart);
}

// Allocate a (huge)page backed by physical memory. Suitable for DMA.
// The memory block is zero-initialized by the kernel
void *AllocHugepage(Hugepage type);

// Deallocate a page allocated by AllocHugepage()
void FreeHugepage(void *ptr);

}  // namespace bess

#endif  // BESS_PACKET_H_
