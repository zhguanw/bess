#include "packet.h"

#include <glog/logging.h>
#include <rte_errno.h>

#include <cassert>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>

#include "dpdk.h"
#include "opts.h"
#include "packet_pool.h"
#include "utils/common.h"

namespace bess {

namespace {

Packet *paddr_to_snb_memchunk(struct rte_mempool_memhdr *chunk,
                                     phys_addr_t paddr) {
  if (chunk->phys_addr == RTE_BAD_PHYS_ADDR) {
    return nullptr;
  }

  if (chunk->phys_addr <= paddr && paddr < chunk->phys_addr + chunk->len) {
    uintptr_t vaddr;

    vaddr = (uintptr_t)chunk->addr + paddr - chunk->phys_addr;
    return reinterpret_cast<Packet *>(vaddr);
  }

  return nullptr;
}

}  // namespace (anonymous)

#define check_offset(field)                                                                                                                                                                                                                                                                                                  \
  do {                                                                                                                                                                                                                                                                                                                \
    static_assert(offsetof(Packet, field##_) == offsetof(rte_mbuf, field), \
      "Incompatibility detected between class Packet and struct rte_mbuf"); \
  } while (0)

Packet::Packet() {
  // static assertions for rte_mbuf layout compatibility
  static_assert(offsetof(Packet, mbuf_) == 0, "mbuf_ must be at offset 0");
  check_offset(buf_addr);
  check_offset(rearm_data);
  check_offset(data_off);
  check_offset(refcnt);
  check_offset(nb_segs);
  check_offset(rx_descriptor_fields1);
  check_offset(pkt_len);
  check_offset(data_len);
  check_offset(buf_len);
  check_offset(pool);
  check_offset(next);

  rte_pktmbuf_reset(&mbuf_);
}

#undef check_offset

Packet *Packet::from_paddr(phys_addr_t paddr) {
  for (int sid = 0; sid < RTE_MAX_NUMA_NODES; sid++) {
    struct rte_mempool_memhdr *chunk;

    struct rte_mempool *pool = PacketPool::GetDefaultPool(sid)->pool();
    if (!pool) {
      continue;
    }

    STAILQ_FOREACH(chunk, &pool->mem_list, next) {
      Packet *pkt = paddr_to_snb_memchunk(chunk, paddr);
      if (!pkt) {
        continue;
      }

      if (pkt->paddr() != paddr) {
        LOG(ERROR) << "pkt->immutable.paddr corruption: pkt=" << pkt
                   << ", pkt->immutable.paddr=" << pkt->paddr()
                   << " (!= " << paddr << ")";
        return nullptr;
      }

      return pkt;
    }
  }

  return nullptr;
}

Packet *Packet::copy(const Packet *src) {
  DCHECK(src->is_linear());

  Packet *dst = reinterpret_cast<Packet *>(rte_pktmbuf_alloc(src->pool_));
  if (!dst) {
    return nullptr;  // FAIL.
  }

  bess::utils::CopyInlined(dst->append(src->total_len()), src->head_data(),
                           src->total_len(), true);

  return dst;
}

// basically rte_hexdump() from eal_common_hexdump.c
static std::string HexDump(const void *buffer, size_t len) {
  std::ostringstream dump;
  size_t i, ofs;
  const char *data = reinterpret_cast<const char *>(buffer);

  dump << "Dump data at [" << buffer << "], len=" << len << std::endl;
  ofs = 0;
  while (ofs < len) {
    dump << std::setfill('0') << std::setw(8) << std::hex << ofs << ":";
    for (i = 0; ((ofs + i) < len) && (i < 16); i++) {
      dump << " " << std::setfill('0') << std::setw(2) << std::hex
           << (data[ofs + i] & 0xFF);
    }
    for (; i <= 16; i++) {
      dump << " | ";
    }
    for (i = 0; (ofs < len) && (i < 16); i++, ofs++) {
      char c = data[ofs];
      if ((c < ' ') || (c > '~')) {
        c = '.';
      }
      dump << c;
    }
    dump << std::endl;
  }
  return dump.str();
}

std::string Packet::Dump() {
  std::ostringstream dump;
  Packet *pkt;
  uint32_t dump_len = total_len();
  uint32_t nb_segs;
  uint32_t len;

  dump << "refcnt chain: ";
  for (pkt = this; pkt; pkt = pkt->next_) {
    dump << pkt->refcnt_ << ' ';
  }
  dump << std::endl;

  dump << "pool chain: ";
  for (pkt = this; pkt; pkt = pkt->next_) {
    dump << pkt->pool_ << " ";
  }
  dump << std::endl;

  dump << "dump packet at " << this << ", phys=" << buf_physaddr_
       << ", buf_len=" << buf_len_ << std::endl;
  dump << "  pkt_len=" << pkt_len_ << ", ol_flags=" << std::hex
       << mbuf_.ol_flags << ", nb_segs=" << std::dec << nb_segs_
       << ", in_port=" << mbuf_.port << std::endl;

  nb_segs = nb_segs_;
  pkt = this;
  while (pkt && nb_segs != 0) {
    __rte_mbuf_sanity_check(&pkt->as_rte_mbuf(), 0);

    dump << "  segment at " << pkt << ", data=" << pkt->head_data()
         << ", data_len=" << std::dec << unsigned{data_len_} << std::endl;

    len = total_len();
    if (len > data_len_) {
      len = data_len_;
    }

    if (len != 0) {
      dump << HexDump(head_data(), len);
    }

    dump_len -= len;
    pkt = pkt->next_;
    nb_segs--;
  }

  return dump.str();
}

}  // namespace bess
