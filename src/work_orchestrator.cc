/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Distributed under BSD 3-Clause license.                                   *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Illinois Institute of Technology.                        *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of Hermes. The full Hermes copyright notice, including  *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the top directory. If you do not  *
 * have access to the file, you may request a copy from help@hdfgroup.org.   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "chimaera/work_orchestrator/work_orchestrator.h"
#include "chimaera/work_orchestrator/worker.h"
#include "chimaera/module_registry/task.h"

namespace chi {

void WorkOrchestrator::ServerInit(ServerConfig *config, QueueManager &qm) {
  config_ = config;

  // Initialize argobots
  ABT_init(0, nullptr);

  // Create thread-local storage key
  CreateThreadLocalBlock();

  // Monitoring information
  monitor_gap_ = config_->wo_.monitor_gap_;
  monitor_window_ = config_->wo_.monitor_window_;

  // Spawn workers on the stream
  size_t num_workers = config_->wo_.max_dworkers_ + config->wo_.max_oworkers_;
  workers_.reserve(num_workers);
  int worker_id = 0;
  // Spawn dedicated workers (dworkers)
  u32 num_dworkers = config_->wo_.max_dworkers_;
  int cpu_id = 0;
  for (; worker_id < num_dworkers; ++worker_id) {
    ABT_xstream xstream = MakeXstream();
    workers_.emplace_back(std::make_unique<Worker>(
        worker_id, cpu_id, xstream));
    Worker &worker = *workers_.back();
    worker.EnableContinuousPolling();
    worker.SetLowLatency();
    dworkers_.emplace_back(&worker);
    ++cpu_id;
  }
  // Spawn reinforcement thread
  reinforce_worker_ = std::make_unique<ReinforceWorker>(
      cpu_id + 1);
  // Spawn overlapped workers (oworkers)
  for (size_t i = 0; i < num_workers - worker_id; ++worker_id) {
    ABT_xstream xstream;
    if (i % config->wo_.owork_per_core_ == 0) {
      xstream = MakeXstream();
      ++cpu_id;
    }
    workers_.emplace_back(
        std::make_unique<Worker>(worker_id, cpu_id, xstream));
    Worker &worker = *workers_.back();
    worker.DisableContinuousPolling();
    worker.SetHighLatency();
    oworkers_.emplace_back(&worker);
  }
  kill_requested_ = false;
  // Create RPC worker threads
  rpc_pool_ = tl::pool::create(tl::pool::access::mpmc);
  ++cpu_id;
  for(int i = 0; i < CHI_RPC->num_threads_; i++) {
    tl::managed<tl::xstream> es
        = tl::xstream::create(tl::scheduler::predef::deflt, *rpc_pool_);
    es->set_cpubind(cpu_id);
    rpc_xstreams_.push_back(std::move(es));
    ++cpu_id;
    if (cpu_id > HERMES_SYSTEM_INFO->ncpu_) {
      cpu_id = num_dworkers + 1;
    }
  }
  HILOG(kInfo, "(node {}) Worker created RPC pool with {} threads",
        CHI_RPC->node_id_, CHI_RPC->num_threads_)

  // Wait for pids to become non-zero
  while (true) {
    bool all_pids_nonzero = true;
    for (std::unique_ptr<Worker> &worker : workers_) {
      if (worker->pid_ == 0) {
        all_pids_nonzero = false;
        break;
      }
    }
    if (all_pids_nonzero) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // Dedicate CPU cores to this runtime
  DedicateCores();

  // Assign ingress queues to workers
  size_t count_lowlat_ = 0;
  size_t count_highlat_ = 0;
  for (ingress::MultiQueue &queue : *CHI_QM_RUNTIME->queue_map_) {
    if (queue.id_.IsNull() || !queue.flags_.Any(QUEUE_READY)) {
      continue;
    }
    for (ingress::LaneGroup &lane_group : queue.groups_) {
      u32 num_lanes = lane_group.num_lanes_;
      for (LaneId lane_id = lane_group.num_scheduled_; lane_id < num_lanes; ++lane_id) {
        u32 worker_id;
        if (lane_group.IsLowLatency()) {
          u32 worker_off = count_lowlat_ % CHI_WORK_ORCHESTRATOR->dworkers_.size();
          count_lowlat_ += 1;
          Worker &worker = *CHI_WORK_ORCHESTRATOR->dworkers_[worker_off];
          worker.PollQueues({WorkEntry(lane_group.prio_, lane_id, &queue)});
          worker_id = worker.id_;
//            HILOG(kInfo, "(node {}) Scheduling the queue {} (prio {}, lane {}, worker {})",
//                  CHI_CLIENT->node_id_, queue.id_, lane_group.prio_, lane_id, worker.id_);
        } else {
          u32 worker_off = count_highlat_ % CHI_WORK_ORCHESTRATOR->oworkers_.size();
          count_highlat_ += 1;
          Worker &worker = *CHI_WORK_ORCHESTRATOR->oworkers_[worker_off];
          worker.PollQueues({WorkEntry(lane_group.prio_, lane_id, &queue)});
          worker_id = worker.id_;
        }
        ingress::Lane &lane = lane_group.GetLane(lane_id);
        lane.worker_id_ = worker_id;
      }
      lane_group.num_scheduled_ = num_lanes;
    }
  }

  HILOG(kInfo, "(node {}) Started {} workers",
        CHI_RPC->node_id_, num_workers);
}

void WorkOrchestrator::Join() {
  kill_requested_.store(true);
  for (std::unique_ptr<Worker> &worker : workers_) {
    worker->Join();
  }
}

/** Get worker with this id */
Worker& WorkOrchestrator::GetWorker(u32 worker_id) {
  return *workers_[worker_id];
}

/** Get the number of workers */
size_t WorkOrchestrator::GetNumWorkers() {
  return workers_.size();
}

/** Get all PIDs of active workers */
std::vector<int> WorkOrchestrator::GetWorkerPids() {
  std::vector<int> pids;
  pids.reserve(workers_.size());
  for (std::unique_ptr<Worker> &worker : workers_) {
    pids.push_back(worker->pid_);
  }
  return pids;
}

/** Get the complement of worker cores */
std::vector<int> WorkOrchestrator::GetWorkerCoresComplement() {
  std::vector<int> cores;
  cores.reserve(HERMES_SYSTEM_INFO->ncpu_);
  for (int i = 0; i < HERMES_SYSTEM_INFO->ncpu_; ++i) {
    cores.push_back(i);
  }
  for (std::unique_ptr<Worker> &worker : workers_) {
    cores.erase(std::remove(cores.begin(), cores.end(), worker->affinity_), cores.end());
  }
  return cores;
}

/** Dedicate cores */
void WorkOrchestrator::DedicateCores() {
  ProcessAffiner affiner;
  std::vector<int> worker_pids = GetWorkerPids();
  std::vector<int> cpu_ids = GetWorkerCoresComplement();
  affiner.IgnorePids(worker_pids);
  affiner.SetCpus(cpu_ids);
  int count = affiner.AffineAll();
  // HILOG(kInfo, "Affining {} processes to {} cores", count, cpu_ids.size());
}

std::vector<Load> WorkOrchestrator::CalculateLoad() {
  std::vector<Load> loads(workers_.size());
  for (auto pool_it = CHI_MOD_REGISTRY->pools_.begin();
       pool_it != CHI_MOD_REGISTRY->pools_.end(); ++pool_it) {
    for (auto cont_it = pool_it->second.containers_.begin();
         cont_it != pool_it->second.containers_.end(); ++cont_it) {
      Container *container = cont_it->second;
      for (auto lane_grp_it = container->lane_groups_.begin();
           lane_grp_it != container->lane_groups_.end(); ++lane_grp_it) {
        LaneGroup &lane_grp = *lane_grp_it->second;
        for (auto lane_it = lane_grp.lanes_.begin();
             lane_it != lane_grp.lanes_.end(); ++lane_it) {
          Lane &lane = *lane_it;
          Load &load = loads[lane.worker_id_];
          load += lane.load_;
        }
      }
    }
  }
  return loads;
}

#ifdef CHIMAERA_ENABLE_PYTHON
void WorkOrchestrator::RegisterPath(const std::string &path) {
  CHI_PYTHON->RegisterPath(path);
}
void WorkOrchestrator::ImportModule(const std::string &name) {
    CHI_PYTHON->ImportModule(name);
}
void WorkOrchestrator::RunString(const std::string &script) {
    CHI_PYTHON->RunString(script);
}
void WorkOrchestrator::RunFunction(const std::string &func_name,
                                   PyDataWrapper &data) {
    CHI_PYTHON->RunFunction(func_name, data);
}
void WorkOrchestrator::RunMethod(const std::string &class_name,
                                 const std::string &method_name,
                                 PyDataWrapper &data) {
  CHI_PYTHON->RunMethod(class_name, method_name, data);
}
#endif

}  // namespace chi
