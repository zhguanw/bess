#ifndef BESS_PACKET_POOL_H_
#define BESS_PACKET_POOL_H_

#include "packet.h"
#include "worker.h"

namespace bess {

// PacketPool is a C++ wrapper for DPDK rte_mempool. It has a pool of
// pre-populated Packet objects, which can be fetched via Alloc().
// Alloc() and Free(), are thread-safe.
class PacketPool {
 public:
  PacketPool(size_t initial_size = kDefaultSize);
  virtual ~PacketPool();

  // PacketPool is neither copyable nor movable.
  PacketPool(const PacketPool &) = delete;
  PacketPool operator=(const PacketPool &) = delete;

  // Allocate a packet from the pool, with specified initial packet size.
  Packet *Alloc(size_t len = 0);

  // Allocate multiple packets. Note that this function has no partial success;
  // it allocates either all "count" packets (returns true) or none (false).
  bool AllocBulk(Packet **pkts, size_t count, size_t len = 0);

  // The number of total packets in the pool.
  size_t capacity() const { return pool_->size; }

  // The number of available packets in the pool. Approximate by nature.
  size_t size() const { return rte_mempool_avail_count(pool_); }

  rte_mempool *pool() { return pool_; }

  // Default packet pool for current NUMA node
  static PacketPool *GetDefaultPool() { return GetDefaultPool(ctx.socket()); }
  static PacketPool *GetDefaultPool(int node) { return default_pools_[node]; }

  static void CreateDefaultPools();

 private:
  static const size_t kDefaultSize = (1 << 16) - 1;
  static const size_t kCacheSize = 512;  // per-core cache size

  static int next_id_;

  // Default per-node packet pools
  static PacketPool *default_pools_[RTE_MAX_NUMA_NODES];

  rte_mempool *pool_;
  std::string pool_name_;

  friend class Packet;
};

inline Packet *PacketPool::Alloc(size_t len) {
  rte_mbuf *mbuf = rte_pktmbuf_alloc(pool_);

  Packet *pkt = reinterpret_cast<Packet *>(mbuf);
  if (pkt) {
    pkt->pkt_len_ = len;
    pkt->data_len_ = len;

    // TODO: sanity check
  }

  return pkt;
}

inline bool PacketPool::AllocBulk(Packet **pkts, size_t count, size_t len) {
  if (rte_mempool_get_bulk(ctx.packet_pool()->pool_,
                           reinterpret_cast<void **>(pkts), count) < 0) {
    return false;
  }

  // We must make sure that the following 12 fields are initialized
  // as done in rte_pktmbuf_reset(). We group them into two 16-byte stores.
  //
  // - 1st store: mbuf.rearm_data
  //   2B data_off == RTE_PKTMBUF_HEADROOM (SNBUF_HEADROOM)
  //   2B refcnt == 1
  //   2B nb_segs == 1
  //   2B port == 0xff (0xffff should make more sense)
  //   8B ol_flags == 0
  //
  // - 2nd store: mbuf.rx_descriptor_fields1
  //   4B packet_type == 0
  //   4B pkt_len == len
  //   2B data_len == len
  //   2B vlan_tci == 0
  //   4B (rss == 0)       (not initialized by rte_pktmbuf_reset)
  //
  // We can ignore these fields:
  //   vlan_tci_outer == 0 (not required if ol_flags == 0)
  //   tx_offload == 0     (not required if ol_flags == 0)
  //   next == nullptr     (all packets in a mempool must already be nullptr)

  __m128i rearm = _mm_setr_epi16(SNBUF_HEADROOM, 1, 1, 0xff, 0, 0, 0, 0);
  __m128i rxdesc = _mm_setr_epi32(0, len, len, 0);

  size_t i;

  /* 4 at a time didn't help */
  for (i = 0; i < (count & (~0x1)); i += 2) {
    /* since the data is likely to be in the store buffer
     * as 64-bit writes, 128-bit read will cause stalls */
    Packet *pkt0 = pkts[i];
    Packet *pkt1 = pkts[i + 1];

    _mm_store_si128(&pkt0->rearm_data_, rearm);
    _mm_store_si128(&pkt0->rx_descriptor_fields1_, rxdesc);
    _mm_store_si128(&pkt1->rearm_data_, rearm);
    _mm_store_si128(&pkt1->rx_descriptor_fields1_, rxdesc);
  }

  if (count & 0x1) {
    Packet *pkt = pkts[i];

    _mm_store_si128(&pkt->rearm_data_, rearm);
    _mm_store_si128(&pkt->rx_descriptor_fields1_, rxdesc);
  }

  // TODO: sanity check
  return true;
}

}  // namespace bess

#endif  // BESS_PACKET_POOL_H_
