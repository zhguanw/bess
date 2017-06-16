#include "packet_pool.h"

#include <sys/mman.h>

#include <rte_errno.h>
#include <rte_mempool.h>

#include "dpdk.h"
#include "utils/format.h"

namespace bess {
namespace {

struct PoolPrivate {
  rte_pktmbuf_pool_private dpdk_priv;
  PacketPool *owner;
};

// callback function for each packet
void InitPacket(rte_mempool *mp, void *, void *_mbuf, unsigned index) {
  rte_pktmbuf_init(mp, nullptr, _mbuf, index);

  auto *pkt = static_cast<Packet *>(_mbuf);
  pkt->set_vaddr(pkt);
  pkt->set_paddr(rte_mempool_virt2phy(mp, pkt));
}

void DoMunmap(rte_mempool_memhdr *memhdr, void *) {
  if (munmap(memhdr->addr, memhdr->len) < 0) {
    PLOG(WARNING) << "munmap()";
  }
}

}  // namespace (anonymous)

PacketPool *PacketPool::default_pools_[RTE_MAX_NUMA_NODES];

PacketPool::PacketPool(size_t capacity, int socket_id) {
  if (!IsDpdkInitialized()) {
    InitDpdk();
  }

  static int next_id_;
  std::string name = utils::Format("PacketPool%d", next_id_++);

  PoolPrivate priv = {
      .dpdk_priv = {.mbuf_data_room_size = SNBUF_HEADROOM + SNBUF_DATA,
                    .mbuf_priv_size = SNBUF_RESERVE},
      .owner = this};

  pool_ = rte_mempool_create_empty(name.c_str(), capacity, sizeof(Packet),
                                   capacity > 1024 ? kMaxCacheSize : 0,
                                   sizeof(priv), socket_id, 0);
  if (!pool_) {
    LOG(FATAL) << "rte_mempool_create() failed: " << rte_strerror(rte_errno)
               << " (rte_errno=" << rte_errno << ")";
  }

  int ret = rte_mempool_set_ops_byname(pool_, "ring_mp_mc", NULL);
  if (ret < 0) {
    LOG(FATAL) << "rte_mempool_set_ops_byname() returned " << ret;
  }

  Populate();

  rte_pktmbuf_pool_init(pool_, &priv.dpdk_priv);
  rte_mempool_obj_iter(pool_, InitPacket, nullptr);

  LOG(INFO) << name << " has been created with " << Capacity() << "/"
            << capacity << " packets";
  if (Capacity() == 0) {
    LOG(FATAL) << name << " has no packets allocated\n"
               << "Troubleshooting:\n"
               << "  - Check 'ulimit -l'\n"
               << "  - Do you have enough memory on the machine?\n"
               << "  - Maybe memory is too fragmented. Try rebooting.\n";
  }
}

PacketPool::~PacketPool() {
  rte_mempool_free(pool_);
}

void PacketPool::CreateDefaultPools(size_t capacity) {
  rte_dump_physmem_layout(stdout);

  for (int i = 0; i < RTE_MAX_LCORE; i++) {
    int sid = rte_lcore_to_socket_id(i);

    if (!default_pools_[sid]) {
      PacketPool *pool;

      pool = new DpdkPacketPool(capacity, sid);
      default_pools_[sid] = pool;
    }
  }
}

void PacketPool::Populate() {
  pool_->flags |= MEMPOOL_F_NO_PHYS_CONTIG;

  size_t page_shift = __builtin_ffs(getpagesize());
  size_t element_size =
      pool_->header_size + pool_->elt_size + pool_->trailer_size;
  size_t size = rte_mempool_xmem_size(pool_->size, element_size, page_shift);

  void *addr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (addr == MAP_FAILED) {
    PLOG(FATAL) << "mmap()";
  }

  // No error check, as we do not provide a guarantee that memory is pinned.
  int ret = mlock(addr, size);
  pinned_ = (ret == 0);  // may fail as non-root users have mlock limit

  ret = rte_mempool_populate_phys(pool_, static_cast<char *>(addr),
                                  RTE_BAD_PHYS_ADDR, size, DoMunmap, nullptr);
  if (ret < static_cast<ssize_t>(pool_->size)) {
    LOG(WARNING) << "rte_mempool_populate_phys() returned " << ret
                 << " (rte_errno=" << rte_errno << ", "
                 << rte_strerror(rte_errno) << ")";
  }
}

void DpdkPacketPool::Populate() {
  int ret = rte_mempool_populate_default(pool_);
  if (ret < static_cast<ssize_t>(pool_->size)) {
    LOG(WARNING) << "rte_mempool_populate_default() returned " << ret
                 << " (rte_errno=" << rte_errno << ", "
                 << rte_strerror(rte_errno) << ")";
  }
}

}  // namespace bess
