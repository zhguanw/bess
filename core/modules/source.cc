#include "source.h"

const Commands Source::cmds = {
    {"set_pkt_size", "SourceCommandSetPktSizeArg",
     MODULE_CMD_FUNC(&Source::CommandSetPktSize), 1},
    {"set_burst", "SourceCommandSetBurstArg",
     MODULE_CMD_FUNC(&Source::CommandSetBurst), 1},
};

CommandResponse Source::Init(const bess::pb::SourceArg &arg) {
  CommandResponse err;

  task_id_t tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return CommandFailure(ENOMEM, "Task creation failed");

  pkt_size_ = 60;
  burst_ = bess::PacketBatch::kMaxBurst;

  if (arg.pkt_size() > 0) {
    if (arg.pkt_size() > SNBUF_DATA) {
      return CommandFailure(EINVAL, "Invalid packet size");
    }
    pkt_size_ = arg.pkt_size();
  }

  burst_ = bess::PacketBatch::kMaxBurst;

  return CommandSuccess();
}

CommandResponse Source::CommandSetBurst(
    const bess::pb::SourceCommandSetBurstArg &arg) {
  if (arg.burst() > bess::PacketBatch::kMaxBurst) {
    return CommandFailure(EINVAL, "burst size must be [0,%zu]",
                          bess::PacketBatch::kMaxBurst);
  }
  burst_ = arg.burst();

  return CommandSuccess();
}

CommandResponse Source::CommandSetPktSize(
    const bess::pb::SourceCommandSetPktSizeArg &arg) {
  uint64_t val = arg.pkt_size();
  if (val == 0 || val > SNBUF_DATA) {
    return CommandFailure(EINVAL, "Invalid packet size");
  }
  pkt_size_ = val;
  return CommandSuccess();
}

struct task_result Source::RunTask(void *) {
  if (children_overload_ > 0) {
    return {.block = true, .packets = 0, .bits = 0};
  }

  bess::PacketBatch batch;

  const size_t pkt_overhead = 24;

  const size_t pkt_size = ACCESS_ONCE(pkt_size_);
  const size_t burst = ACCESS_ONCE(burst_);

  if (!ctx.packet_pool()->AllocBulk(batch.pkts(), burst, pkt_size)) {
    return {.block = false, .packets = 0, .bits = 0};
  }

  batch.set_cnt(burst);
  RunNextModule(&batch);

  return {.block = false,
          .packets = static_cast<uint32_t>(burst),
          .bits = (pkt_size + pkt_overhead) * burst * 8};
}

ADD_MODULE(Source, "source",
           "infinitely generates packets with uninitialized data")
