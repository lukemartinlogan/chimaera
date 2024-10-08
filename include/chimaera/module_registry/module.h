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

#ifndef CHI_INCLUDE_CHI_TASK_TASK_H_
#define CHI_INCLUDE_CHI_TASK_TASK_H_

#include <dlfcn.h>
#include "chimaera/chimaera_types.h"
#include "chimaera/queue_manager/queue_factory.h"
#include "task.h"
#include "chimaera/network/serialize_defn.h"
#include "chimaera/work_orchestrator/comutex_defn.h"
#include "chimaera/work_orchestrator/corwlock_defn.h"

namespace chi {

typedef LPointer<Task> TaskPointer;

/** The information of a lane */
class Lane {
 public:
  Lane *next_;
  Lane *prior_;
  QueueId lane_id_;
  QueueId ingress_id_;
  i32 worker_id_;
  size_t num_tasks_ = 0;
  Load load_;
  std::unordered_map<TaskId, int> active_;
  CoMutex comux_;
  std::atomic<size_t> plug_count_;
  u32 prio_;

 public:
  /** Emplace constructor */
  explicit Lane(QueueId lane_id, QueueId ingress_id, i32 worker_id, u32 prio) :
  lane_id_(lane_id), ingress_id_(ingress_id),
  worker_id_(worker_id), prio_(prio) {
    plug_count_ = 0;
  }

  /** Copy constructor */
  Lane(const Lane &lane) {
    lane_id_ = lane.lane_id_;
    ingress_id_ = lane.ingress_id_;
    worker_id_ = lane.worker_id_;
    num_tasks_ = lane.num_tasks_;
    load_ = lane.load_;
    active_ = lane.active_;
    plug_count_ = lane.plug_count_.load();
    prio_ = lane.prio_;
  }

  bool IsActive(TaskId id) {
    return active_.find(id) != active_.end();
  }

  void SetActive(TaskId id) {
    if (!IsActive(id)) {
      active_[id] = 1;
    } else {
      active_[id] += 1;
    }
  }

  void UnsetActive(TaskId id) {
    if (IsActive(id)) {
      active_[id] -= 1;
      if (active_[id] == 0) {
        active_.erase(id);
      }
    }
  }

  size_t GetNumActive() {
    return active_.size();
  }

  bool IsPlugged() {
    return plug_count_ > 0;
  }

  void SetPlugged() {
    plug_count_ += 1;
  }

  void UnsetPlugged() {
    plug_count_ -= 1;
  }
};

/** A group of lanes */
struct LaneGroup {
  std::vector<Lane> lanes_;
};

/**
 * Represents a custom operation to perform.
 * Tasks are independent of Hermes.
 * */
class Module {
 public:
  PoolId id_;    /**< The unique name of a pool */
  QueueId queue_id_;  /**< The queue id of a pool */
  std::string name_;  /**< The unique semantic name of a pool */
  ContainerId container_id_;       /**< The logical id of a container */
  LaneId lane_counter_ = 0;
  std::unordered_map<LaneGroupId, std::shared_ptr<LaneGroup>>
      lane_groups_;  /**< The lanes of a pool */
  bitfield32_t mod_flags_;

  /** Default constructor */
  Module() : id_(PoolId::GetNull()) {}

  /** Emplace Constructor */
  void Init(const PoolId &id, const QueueId &queue_id,
            const std::string &name) {
    id_ = id;
    queue_id_ = queue_id;
    name_ = name;
  }

  /** Create a lane group */
  void CreateLaneGroup(const LaneGroupId &id, u32 count, u32 flags);

  /** Get lane */
  Lane* GetLaneByHash(const LaneGroupId &group, u32 hash) {
    LaneGroup &lane_group = *lane_groups_[group];
    return &lane_group.lanes_[hash % lane_group.lanes_.size()];
  }

  /** Get lane with the least load */
  template<typename F>
  Lane* GetLeastLoadedLane(const LaneGroupId &group, F&& func) {
    LaneGroup &lane_group = *lane_groups_[group];
    Lane *least_loaded = &lane_group.lanes_[0];
    for (Lane &lane : lane_group.lanes_) {
      if (func(lane.load_, least_loaded->load_)) {
        least_loaded = &lane;
      }
    }
    return least_loaded;
  }

  /** Get lane */
  Lane* GetLane(const QueueId &lane_id) {
    return GetLaneByHash(lane_id.node_id_, lane_id.unique_);
  }

  /** Plug all lanes */
  void PlugAllLanes() {
    for (auto &lane_group : lane_groups_) {
      for (Lane &lane : lane_group.second->lanes_) {
        lane.UnsetPlugged();
      }
    }
  }

  /** Unplug all lanes */
  void UnplugAllLanes() {
    for (auto &lane_group : lane_groups_) {
      for (Lane &lane : lane_group.second->lanes_) {
        lane.UnsetPlugged();
      }
    }
  }

  /** Get number of active tasks */
  size_t GetNumActiveTasks() {
    size_t num_active = 0;
    for (auto &lane_group : lane_groups_) {
      for (Lane &lane : lane_group.second->lanes_) {
        num_active += lane.GetNumActive();
      }
    }
    return num_active;
  }

  /** Virtual destructor */
  virtual ~Module() = default;

  /** Route to a virtual lane */
  virtual Lane* Route(const Task *task) = 0;

  /** Run a method of the task */
  virtual void Run(u32 method, Task *task, RunContext &rctx) = 0;

  /** Monitor a method of the task */
  virtual void Monitor(MonitorModeId mode,
                       u32 method,
                       Task *task, RunContext &rctx) = 0;

  /** Delete a task */
  virtual void Del(u32 method, Task *task) = 0;

  /** Duplicate a task into an existing task */
  virtual void CopyStart(u32 method,
                         const Task *orig_task,
                         Task *dup_task,
                         bool deep) = 0;

  /** Duplicate a task into a new task */
  virtual void NewCopyStart(u32 method,
                            const Task *orig_task,
                            LPointer<Task> &dup_task,
                            bool deep) = 0;

  /** Serialize a task when initially pushing into remote */
  virtual void SaveStart(u32 method, BinaryOutputArchive<true> &ar, Task *task) = 0;

  /** Deserialize a task when popping from remote queue */
  virtual TaskPointer LoadStart(u32 method, BinaryInputArchive<true> &ar) = 0;

  /** Serialize a task when returning from remote queue */
  virtual void SaveEnd(u32 method, BinaryOutputArchive<false> &ar, Task *task) = 0;

  /** Deserialize a task when returning from remote queue */
  virtual void LoadEnd(u32 method, BinaryInputArchive<false> &ar, Task *task) = 0;
};

/** Represents a Module in action */
typedef Module Container;

/** Represents the Module client-side */
class ModuleClient {
 public:
  PoolId id_;
  QueueId queue_id_;

 public:
  /** Init from existing ID */
  void Init(const PoolId &id,
            const QueueId &queue_id) {
    if (id.IsNull()) {
      HELOG(kWarning, "Failed to create pool");
    }
    id_ = id;
    // queue_id_ = QueueId(id_);
    queue_id_ = queue_id;
  }

  /** Init from existing ID */
  void Init(const PoolId &id) {
    if (id.IsNull()) {
      HELOG(kWarning, "Failed to create pool");
    }
    id_ = id;
    // queue_id_ = QueueId(id_);
    queue_id_ = id;
  }
};

extern "C" {
/** Allocate a state (no construction) */
typedef Container* (*alloc_state_t)();
/** New state (with construction) */
typedef Container* (*new_state_t)(
    const chi::PoolId *pool_id, const char *pool_name);
/** Get the name of a task */
typedef const char* (*get_module_name_t)(void);
}  // extern c

/** Used internally by task source file */
#define CHI_TASK_CC(TRAIT_CLASS, TASK_NAME)\
  extern "C" {\
  void* alloc_state(const chi::PoolId *pool_id, const char *pool_name) {\
    chi::Container *exec = reinterpret_cast<chi::Container*>(\
        new TYPE_UNWRAP(TRAIT_CLASS)());\
    return exec;\
  }\
  void* new_state(const chi::PoolId *pool_id, const char *pool_name) {\
    chi::Container *exec = reinterpret_cast<chi::Container*>(\
        new TYPE_UNWRAP(TRAIT_CLASS)());\
    exec->Init(*pool_id, CHI_CLIENT->GetQueueId(*pool_id), pool_name);\
    return exec;\
  }\
  const char* get_module_name(void) { return TASK_NAME; }\
  bool is_chimaera_task_ = true;\
  }

}   // namespace chi

#endif  // CHI_INCLUDE_CHI_TASK_TASK_H_
