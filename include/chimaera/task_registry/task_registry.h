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

#ifndef HRUN_INCLUDE_HRUN_TASK_TASK_REGISTRY_H_
#define HRUN_INCLUDE_HRUN_TASK_TASK_REGISTRY_H_

#include <string>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <filesystem>
#include "task_lib.h"
#include "chimaera/config/config_server.h"
#include "chimaera_admin/chimaera_admin.h"
// #include "chimaera/network/rpc.h"

namespace stdfs = std::filesystem;

namespace chi {

/** All information needed to create a trait */
struct TaskLibInfo {
  void *lib_;  /**< The dlfcn library */
  alloc_state_t alloc_state_;   /**< The create task function */
  get_task_lib_name_t get_task_lib_name; /**< The get task name function */

  /** Default constructor */
  TaskLibInfo() = default;

  /** Destructor */
  ~TaskLibInfo() {
    if (lib_) {
      dlclose(lib_);
    }
  }

  /** Emplace constructor */
  explicit TaskLibInfo(void *lib,
                       alloc_state_t alloc_state,
                       get_task_lib_name_t get_task_name)
      : lib_(lib), alloc_state_(alloc_state),
      get_task_lib_name(get_task_name) {}

  /** Copy constructor */
  TaskLibInfo(const TaskLibInfo &other)
      : lib_(other.lib_),
        alloc_state_(other.alloc_state_),
        get_task_lib_name(other.get_task_lib_name) {}

  /** Move constructor */
  TaskLibInfo(TaskLibInfo &&other) noexcept
      : lib_(other.lib_),
        alloc_state_(other.alloc_state_),
        get_task_lib_name(other.get_task_lib_name) {
    other.lib_ = nullptr;
    other.alloc_state_ = nullptr;
    other.get_task_lib_name = nullptr;
  }
};

struct TaskStateInfo {
  TaskState *shared_state_;
  std::unordered_map<LaneId, TaskState*> states_;
};

/**
 * Stores the registered set of TaskLibs and TaskStates
 * */
class TaskRegistry {
 public:
  /** The node the registry is on */
  NodeId node_id_;
  /** The dirs to search for task libs */
  std::vector<std::string> lib_dirs_;
  /** Map of a semantic lib name to lib info */
  std::unordered_map<std::string, TaskLibInfo> libs_;
  /** Map of a semantic exec name to exec id */
  std::unordered_map<std::string, TaskStateId> task_state_ids_;
  /** Map of a semantic exec id to state */
  std::unordered_map<TaskStateId, TaskStateInfo> task_states_;
  /** A unique identifier counter */
  std::atomic<u64> *unique_;
  RwLock lock_;

 public:
  /** Default constructor */
  TaskRegistry() {}

  /** Initialize the Task Registry */
  void ServerInit(ServerConfig *config, NodeId node_id, std::atomic<u64> &unique) {
    node_id_ = node_id;
    unique_ = &unique;

    // Load the LD_LIBRARY_PATH variable
    auto ld_lib_path_env = getenv("LD_LIBRARY_PATH");
    std::string ld_lib_path;
    if (ld_lib_path_env) {
      ld_lib_path = ld_lib_path_env;
    }

    // Load the HRUN_TASK_PATH variable
    std::string hermes_lib_path;
    auto hermes_lib_path_env = getenv("HRUN_TASK_PATH");
    if (hermes_lib_path_env) {
      hermes_lib_path = hermes_lib_path_env;
    }

    // Combine LD_LIBRARY_PATH and HRUN_TASK_PATH
    std::string paths = hermes_lib_path + ":" + ld_lib_path;
    std::stringstream ss(paths);
    std::string lib_dir;
    while (std::getline(ss, lib_dir, ':')) {
      lib_dirs_.emplace_back(lib_dir);
    }

    // Find each lib in LD_LIBRARY_PATH
    for (const std::string &lib_name : config->task_libs_) {
      if (!RegisterTaskLib(lib_name)) {
        HELOG(kWarning, "Failed to load the lib: {}", lib_name);
      }
    }
  }

  /** Load a task lib */
  bool RegisterTaskLib(const std::string &lib_name) {
    std::string lib_dir;
    for (const std::string &lib_dir : lib_dirs_) {
      // Determine if this directory contains the library
      std::string lib_path1 = hshm::Formatter::format("{}/{}.so",
                                                      lib_dir,
                                                      lib_name);
      std::string lib_path2 = hshm::Formatter::format("{}/lib{}.so",
                                                      lib_dir,
                                                      lib_name);
      std::string lib_path;
      if (stdfs::exists(lib_path1)) {
        lib_path = std::move(lib_path1);
      } else if (stdfs::exists(lib_path2)) {
        lib_path = std::move(lib_path2);
      } else {
        continue;
      }

      // Load the library
      TaskLibInfo info;
      info.lib_ = dlopen(lib_path.c_str(), RTLD_GLOBAL | RTLD_NOW);
      if (!info.lib_) {
        HELOG(kError, "Could not open the lib library: {}. Reason: {}", lib_path, dlerror());
        return false;
      }
      info.alloc_state_ = (alloc_state_t)dlsym(
          info.lib_, "alloc_state");
      if (!info.alloc_state_) {
        HELOG(kError, "The lib {} does not have alloc_state symbol",
              lib_path);
        return false;
      }
      info.get_task_lib_name = (get_task_lib_name_t)dlsym(
          info.lib_, "get_task_lib_name");
      if (!info.get_task_lib_name) {
        HELOG(kError, "The lib {} does not have get_task_lib_name symbol",
              lib_path);
        return false;
      }
      std::string task_lib_name = info.get_task_lib_name();
      HILOG(kInfo, "Finished loading the lib: {}", task_lib_name)
      libs_.emplace(task_lib_name, std::move(info));
      return true;
    }
    HELOG(kError, "Could not find the lib: {}", lib_name);
    return false;
  }

  /** Destroy a task lib */
  void DestroyTaskLib(const std::string &lib_name) {
    auto it = libs_.find(lib_name);
    if (it == libs_.end()) {
      HELOG(kError, "Could not find the task lib: {}", lib_name);
      return;
    }
    libs_.erase(it);
  }

  /** Allocate a task state ID */
  HSHM_ALWAYS_INLINE
  TaskStateId CreateTaskStateId() {
    return TaskStateId(node_id_, unique_->fetch_add(1));
  }

  /** Check if task state exists by ID */
  HSHM_ALWAYS_INLINE
  bool TaskStateExists(const TaskStateId &state_id) {
    ScopedRwReadLock lock(lock_, 0);
    auto it = task_states_.find(state_id);
    return it != task_states_.end();
  }

  /**
   * Create a task state
   * state_id must not be NULL.
   * */
  bool CreateTaskState(const char *lib_name,
                       const char *state_name,
                       const TaskStateId &state_id,
                       Admin::CreateTaskStateTask *task,
                       const std::vector<SubDomainId> &lanes) {
    // Ensure state_id is not NULL
    if (state_id.IsNull()) {
      HELOG(kError, "The task state ID cannot be null");
      task->SetModuleComplete();
      return false;
    }
//    HILOG(kInfo, "(node {}) Creating an instance of {} with name {}",
//          CHI_CLIENT->node_id_, lib_name, state_name)

    // Find the task library to instantiate
    auto it = libs_.find(lib_name);
    if (it == libs_.end()) {
      HELOG(kError, "Could not find the task lib: {}", lib_name);
      task->SetModuleComplete();
      return false;
    }
    TaskLibInfo &info = it->second;

    // Create shared state
    if (task_states_.find(state_id) == task_states_.end()) {
      // Allocate the state
      task_states_[state_id] = TaskStateInfo();
      TaskState *exec = info.alloc_state_(task, state_name);
      if (!exec) {
        HELOG(kError, "Could not create the task state: {}", state_name);
        task->SetModuleComplete();
        return false;
      }

      // Add the state to the registry
      exec->id_ = state_id;
      exec->name_ = state_name;
      exec->lane_id_ = 0;
      ScopedRwWriteLock lock(lock_, 0);
      task_state_ids_.emplace(state_name, state_id);
      task_states_[state_id].shared_state_ = exec;

      // Construct the state
      task->ctx_.id_ = state_id;
      task->rctx_.shared_exec_ = task_states_[state_id].shared_state_;
      exec->Run(TaskMethod::kCreate, task, task->rctx_);
      task->UnsetModuleComplete();
    }

    // Create partitioned state
    std::unordered_map<LaneId, TaskState*> &states =
        task_states_[state_id].states_;
    for (const SubDomainId &lane_id : lanes) {
      // Don't repeat if state exists
      if (states.find(lane_id.minor_) != states.end()) {
        continue;
      }

      // Allocate the state
      TaskState *exec = info.alloc_state_(task, state_name);
      if (!exec) {
        HELOG(kError, "Could not create the task state: {}", state_name);
        task->SetModuleComplete();
        return false;
      }

      // Add the state to the registry
      exec->id_ = state_id;
      exec->name_ = state_name;
      exec->lane_id_ = lane_id.minor_;
      ScopedRwWriteLock lock(lock_, 0);
      task_states_[state_id].states_[exec->lane_id_] = exec;

      // Construct the state
      task->ctx_.id_ = state_id;
      task->rctx_.shared_exec_ = task_states_[state_id].shared_state_;
      exec->Run(TaskMethod::kCreate, task, task->rctx_);
      task->UnsetModuleComplete();
    }
    HILOG(kInfo, "(node {})  Created an instance of {} with name {} and ID {}",
          CHI_CLIENT->node_id_, lib_name, state_name, state_id);
    return true;
  }

  /** Get or create a task state's ID */
  TaskStateId GetOrCreateTaskStateId(const std::string &state_name) {
    ScopedRwReadLock lock(lock_, 0);
    auto it = task_state_ids_.find(state_name);
    if (it == task_state_ids_.end()) {
      TaskStateId state_id = CreateTaskStateId();
      task_state_ids_.emplace(state_name, state_id);
      return state_id;
    }
    return it->second;
  }

  /** Get a task state's ID */
  TaskStateId GetTaskStateId(const std::string &state_name) {
    ScopedRwReadLock lock(lock_, 0);
    auto it = task_state_ids_.find(state_name);
    if (it == task_state_ids_.end()) {
      return TaskStateId::GetNull();
    }
    return it->second;
  }

  /** Get a task state instance */
  TaskState* GetAnyTaskState(const TaskStateId &task_state_id) {
    ScopedRwReadLock lock(lock_, 0);
    auto it = task_states_.find(task_state_id);
    if (it == task_states_.end()) {
      return nullptr;
    }
    return it->second.shared_state_;
  }

  /** Get task state instance by name OR by ID */
  TaskState* GetAnyTaskState(const std::string &task_name,
                             const TaskStateId &task_state_id) {
    ScopedRwReadLock lock(lock_, 0);
    TaskStateId id = GetTaskStateId(task_name);
    if (id.IsNull()) {
      id = task_state_id;
    }
    auto it = task_states_.find(id);
    if (it == task_states_.end()) {
      return nullptr;
    }
    return it->second.shared_state_;
  }

  /** Get a task state instance */
  TaskState* GetTaskState(const TaskStateId &task_state_id,
                          LaneId lane_id) {
    ScopedRwReadLock lock(lock_, 0);
    auto it = task_states_.find(task_state_id);
    if (it == task_states_.end()) {
      return nullptr;
    }
    return it->second.states_[lane_id];
  }

  /** Destroy a task state */
  void DestroyTaskState(const TaskStateId &task_state_id) {
    ScopedRwWriteLock lock(lock_, 0);
    auto it = task_states_.find(task_state_id);
    if (it == task_states_.end()) {
      HELOG(kWarning, "Could not find the task state");
      return;
    }
    TaskStateInfo &task_states = it->second;
    std::string state_name = task_states.states_[0]->name_;
    // TODO(llogan): Iterate over shared_state + states and destroy them
    task_state_ids_.erase(state_name);
    task_states_.erase(it);
  }
};

/** Singleton macro for task registry */
#define CHI_TASK_REGISTRY \
  (&CHI_RUNTIME->task_registry_)
#define HRUN_TASK_REGISTRY CHI_TASK_REGISTRY
}  // namespace chi

#endif  // HRUN_INCLUDE_HRUN_TASK_TASK_REGISTRY_H_
