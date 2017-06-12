#include "packet_pool.h"

#include <rte_errno.h>
#include <rte_mempool.h>

#include "utils/format.h"

namespace bess {

namespace {

// callback function invoked by DPDK mempool for initialization of each packet
void InitPacket(rte_mempool *mp, void *, void *_mbuf, unsigned index) {
  rte_pktmbuf_init(mp, nullptr, _mbuf, index);

  Packet *pkt = static_cast<Packet *>(_mbuf);
  pkt->set_vaddr(pkt);
  pkt->set_paddr(rte_mempool_virt2phy(mp, pkt));
}

}  // namespace (anonymous)

int PacketPool::next_id_ = 0;

PacketPool *PacketPool::default_pools_[RTE_MAX_NUMA_NODES];

PacketPool::PacketPool(size_t initial_size) {
  pool_name_ = utils::Format("PacketPool%d", next_id_++);

  rte_pktmbuf_pool_private pool_priv;
  pool_priv.mbuf_data_room_size = SNBUF_HEADROOM + SNBUF_DATA;
  pool_priv.mbuf_priv_size = SNBUF_RESERVE;

  pool_ = rte_mempool_create(
      pool_name_.c_str(), initial_size, sizeof(Packet), kCacheSize,
      sizeof(rte_pktmbuf_pool_private), rte_pktmbuf_pool_init,
      &pool_priv, InitPacket, nullptr, SOCKET_ID_ANY, 0);
  if (!pool_) {
    LOG(FATAL) << "rte_mempool_create() failed: " << rte_strerror(rte_errno)
               << "(rte_errno=" << rte_errno << ")";
  }

  rte_pktmbuf_pool_init(pool_, &pool_priv);
}

PacketPool::~PacketPool() {
  rte_mempool_free(pool_);
}

void PacketPool::CreateDefaultPools() {
  for (int i = 0; i < RTE_MAX_LCORE; i++) {
    int sid = rte_lcore_to_socket_id(i);

    if (!default_pools_[sid]) {
      default_pools_[sid] = new PacketPool();
    }
  }
}

}  // namespace bess
