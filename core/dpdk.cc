#include "dpdk.h"

#include <syslog.h>
#include <unistd.h>

#include <glog/logging.h>
#include <rte_config.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "opts.h"
#include "utils/format.h"
#include "worker.h"

namespace bess {
namespace {

int get_numa_count() {
  FILE *fp;

  int matched;
  int cnt;

  fp = fopen("/sys/devices/system/node/possible", "r");
  if (!fp) {
    goto fail;
  }

  matched = fscanf(fp, "0-%d", &cnt);
  if (matched == 1) {
    return cnt + 1;
  }

fail:
  if (fp) {
    fclose(fp);
  }

  LOG(INFO) << "/sys/devices/system/node/possible not available. "
            << "Assuming a single-node system...";
  return 1;
}

void disable_syslog() {
  setlogmask(0x01);
}

void enable_syslog() {
  setlogmask(0xff);
}

/* for log messages during rte_eal_init() */
ssize_t dpdk_log_init_writer(void *, const char *data, size_t len) {
  enable_syslog();
  LOG(INFO) << std::string(data, len);
  disable_syslog();
  return len;
}

ssize_t dpdk_log_writer(void *, const char *data, size_t len) {
  LOG(INFO) << std::string(data, len);
  return len;
}

void init_eal(int dpdk_mb_per_socket, int default_core) {
  int rte_argc = 0;
  const char *rte_argv[32];

  int numa_count = get_numa_count();

  int ret;

  std::string opt_master_lcore = utils::Format("%d", RTE_MAX_LCORE - 1);
  std::string opt_lcore_bitmap =
      utils::Format("%d@%d", RTE_MAX_LCORE - 1, default_core);
  std::string opt_socket_mem;

  rte_argv[rte_argc++] = "bessd";
  rte_argv[rte_argc++] = "--master-lcore";
  rte_argv[rte_argc++] = opt_master_lcore.c_str();
  rte_argv[rte_argc++] = "--lcore";
  rte_argv[rte_argc++] = opt_lcore_bitmap.c_str();

  // Disable .rte_config .rte_hugepage_info files to be created, since we are
  // not interested in DPDK primary-secondary process support.
  rte_argv[rte_argc++] = "--no-shconf";

  if (dpdk_mb_per_socket <= 0) {
    rte_argv[rte_argc++] = "--no-huge";
  } else {
    opt_socket_mem = utils::Format("%d", dpdk_mb_per_socket);
    for (int i = 1; i < numa_count; i++) {
      opt_socket_mem += utils::Format("%d", dpdk_mb_per_socket);
    }
    rte_argv[rte_argc++] = "--socket-mem";
    rte_argv[rte_argc++] = opt_socket_mem.c_str();
  }
  rte_argv[rte_argc] = nullptr;

  // reset getopt()
  optind = 0;

  // DPDK creates duplicated outputs (stdout and syslog).
  // We temporarily disable syslog, then set our log handler
  cookie_io_functions_t dpdk_log_init_funcs;
  cookie_io_functions_t dpdk_log_funcs;

  std::memset(&dpdk_log_init_funcs, 0, sizeof(dpdk_log_init_funcs));
  std::memset(&dpdk_log_funcs, 0, sizeof(dpdk_log_funcs));

  dpdk_log_init_funcs.write = &dpdk_log_init_writer;
  dpdk_log_funcs.write = &dpdk_log_writer;

  FILE *org_stdout = stdout;
  stdout = fopencookie(nullptr, "w", dpdk_log_init_funcs);

  disable_syslog();
  ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
  if (ret < 0) {
    LOG(FATAL) << "rte_eal_init() failed: ret = " << ret;
  }

  enable_syslog();
  fclose(stdout);
  stdout = org_stdout;

  rte_openlog_stream(fopencookie(nullptr, "w", dpdk_log_funcs));
}

// Returns the last core ID of all cores, as the default core all threads will
// run on. If the process was run with a limited set of cores (by `taskset`),
// the last one among them will be picked.
int determine_default_core() {
  cpu_set_t set;

  int ret = pthread_getaffinity_np(pthread_self(), sizeof(set), &set);
  if (ret < 0) {
    PLOG(WARNING) << "pthread_getaffinity_np()";
    return 0;  // Core 0 as a fallback
  }

  // Choose the last core available
  for (int i = CPU_SETSIZE; i >= 0; i--) {
    if (CPU_ISSET(i, &set)) {
      return i;
    }
  }

  // This will never happen, but just in case.
  PLOG(WARNING) << "No core is allowed for the process?";
  return 0;
}

bool is_initialized = false;

}  // namespace (anonymous)

bool IsDpdkInitialized() {
  return is_initialized;
}

void InitDpdk() {
  // Isolate all background threads in a separate core.
  // All non-worker threads will be scheduled on default_core,
  // including threads spawned by DPDK and gRPC.
  // FIXME: This is a temporary fix. If a new worker thread is allocated on the
  //        same core, background threads should migrate to another core.
  ctx.SetNonWorker();

  if (!is_initialized) {
    is_initialized = true;
    LOG(INFO) << "Initializing DPDK";
    init_eal(FLAGS_m, determine_default_core());
  }
}

}  // namespace bess
