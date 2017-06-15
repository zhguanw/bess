#include "packet_pool.h"

#include <rte_errno.h>
#include <rte_mempool.h>

#include "utils/format.h"

namespace bess {
namespace {

struct PacketPoolPrivate {
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

}  // namespace (anonymous)

PacketPool *PacketPool::default_pools_[RTE_MAX_NUMA_NODES];

PacketPool::PacketPool(size_t capacity, int socket_id) {
  static int next_id_;
  std::string name = utils::Format("PacketPool%d", next_id_++);

  PacketPoolPrivate priv = {
      .dpdk_priv = {.mbuf_data_room_size = SNBUF_HEADROOM + SNBUF_DATA,
                    .mbuf_priv_size = SNBUF_RESERVE},
      .owner = this};

  pool_ = rte_mempool_create_empty(name.c_str(), capacity, sizeof(Packet),
                                   kCacheSize, sizeof(priv), socket_id, 0);
  if (!pool_) {
    LOG(FATAL) << "rte_mempool_create() failed: " << rte_strerror(rte_errno)
               << " (rte_errno=" << rte_errno << ")";
  }

  Populate(socket_id);

  rte_pktmbuf_pool_init(pool_, &priv.dpdk_priv);
  rte_mempool_obj_iter(pool_, InitPacket, nullptr);
}

PacketPool::~PacketPool() {
  rte_mempool_free(pool_);
}

void PacketPool::CreateDefaultPools() {
  rte_dump_physmem_layout(stdout);

  for (int i = 0; i < RTE_MAX_LCORE; i++) {
    int sid = rte_lcore_to_socket_id(i);

    if (!default_pools_[sid]) {
      default_pools_[sid] = new PacketPool();
    }
  }
}

void PacketPool::Populate(int) {
  rte_mempool_populate_anon(pool_);
}

}  // namespace bess
