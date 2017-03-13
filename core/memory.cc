#include "memory.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <unistd.h>

#include <iostream>

namespace bess {

uintptr_t Virt2PhyGeneric(void *ptr) {
  const uintptr_t kPageSize = sysconf(_SC_PAGESIZE);

  uintptr_t vaddr = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t offset = vaddr % kPageSize;

  int fd = open("/proc/self/pagemap", O_RDONLY);
  if (fd < 0) {
    PLOG(ERROR) << "open(/proc/self/pagemap)";
    return 0;
  }

  uint64_t page_info;
  int ret = pread(fd, &page_info, sizeof(page_info),
                  (vaddr / kPageSize) * sizeof(page_info));
  if (ret != sizeof(page_info)) {
    PLOG(ERROR) << "pread(/proc/self/pagemap)";
  }

  close(fd);

  // See Linux Documentation/vm/pagemap.txt
  // page frame number (physical address / kPageSize) is on lower 55 bits
  uintptr_t pfn = page_info & ((1ull << 55) - 1);
  bool present = page_info & (1ull << 63);

  if (!present) {
    LOG(ERROR) << "Virt2PhyGeneric(): virtual address " << ptr
                 << " is not mapped";
    return 0;
  }

  if (pfn == 0) {
    LOG_FIRST_N(ERROR, 1)
        << "Virt2PhyGeneric(): PFN for vaddr " << ptr
        << " is not available. CAP_SYS_ADMIN capability is required. "
        << "page_info = " << std::hex << page_info << std::dec;
    return 0;
  }

  uintptr_t paddr = pfn * kPageSize + offset;

  return paddr;
}

#ifndef SHM_HUGE_SHIFT
#define SHM_HUGE_SHIFT 26
#endif

#ifndef SHM_HUGE_2MB
#define SHM_HUGE_2MB (21 << SHM_HUGE_SHIFT)
#endif

#ifndef SHM_HUGE_1GB
#define SHM_HUGE_1GB (30 << SHM_HUGE_SHIFT)
#endif

void *AllocHugepage(Hugepage type) {
  int shm_flags = SHM_NORESERVE | IPC_CREAT | 0600;
  size_t size = static_cast<size_t>(type);

  switch (type) {
    case Hugepage::kSize4KB:
      break;
    case Hugepage::kSize2MB:
      shm_flags |= SHM_HUGETLB | SHM_HUGE_2MB;
      break;
    case Hugepage::kSize1GB:
      shm_flags |= SHM_HUGETLB | SHM_HUGE_1GB;
      break;
    default:
      CHECK(0);
  }

  int shm_id = shmget(IPC_PRIVATE, size, shm_flags);
  if (shm_id == -1) {
    PLOG(ERROR) << "shmget() with pagesize = " << size;
    return nullptr;
  }

  void *ptr = shmat(shm_id, nullptr, 0);
  shmctl(shm_id, IPC_RMID, 0);  // won't be freed until everyone detaches

  if (ptr == MAP_FAILED) {
    PLOG(ERROR) << "shmat()";
    return nullptr;
  }

  if (mlock(ptr, size) != 0) {
    PLOG(ERROR) << "mlock(ptr) - check 'ulimit -l'";
    shmdt(ptr);
    return nullptr;
  }

  uintptr_t paddr = Virt2PhyGeneric(ptr);
  if (paddr == 0) {
    LOG(ERROR) << "Virt2PhyGeneric() failed";
    shmdt(ptr);
    return nullptr;
  }

  void *ptr_remapped = shmat(shm_id, Phy2Virt(paddr), 0);
  if (ptr_remapped == MAP_FAILED) {
    PLOG(ERROR) << "shmat() for remapping";
    shmdt(ptr);
    return nullptr;
  }

  // Remove the temporary mapping
  int ret = shmdt(ptr);
  PLOG_IF(ERROR, ret != 0) << "shmdt(ptr)";

  if (mlock(ptr_remapped, size) != 0) {
    PLOG(ERROR) << "mlock(ptr_remapped) - check 'ulimit -l'";
    shmdt(ptr_remapped);
    return nullptr;
  }

  return ptr_remapped;
}

void FreeHugepage(void *ptr) {
  // allow null pointers
  if (ptr) {
    int ret = shmdt(ptr);
    PLOG_IF(ERROR, ret != 0) << "shmdt(ptr_remapped)";
  }
}

}  // namespace bess
