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

#ifndef HRUN_INCLUDE_CHI_CLIENT_CHI_CLIENT_H_
#define HRUN_INCLUDE_CHI_CLIENT_CHI_CLIENT_H_

#include <string>
#include "manager.h"
#include "chimaera/queue_manager/queue_manager_client.h"
#include "chimaera/network/rpc.h"

// Singleton macros
#define CHI_CLIENT hshm::Singleton<chi::Client>::GetInstance()
#define CHI_CLIENT_T chi::Client*
#define HRUN_CLIENT CHI_CLIENT
#define HRUN_CLIENT_T CHI_CLIENT_T

namespace chi {

class Client : public ConfigurationManager {
 public:
  int data_;
  QueueManagerClient queue_manager_;
  std::atomic<u64> *unique_;
  NodeId node_id_;

 public:
  /** Default constructor */
  Client() {}

  /** Initialize the client */
  Client* Create(std::string server_config_path = "",
                 std::string client_config_path = "",
                 bool server = false) {
    hshm::ScopedMutex lock(lock_, 1);
    if (is_initialized_) {
      return this;
    }
    mode_ = HrunMode::kClient;
    is_being_initialized_ = true;
    ClientInit(std::move(server_config_path),
               std::move(client_config_path),
               server);
    is_initialized_ = true;
    is_being_initialized_ = false;
    return this;
  }

 private:
  /** Initialize client */
  void ClientInit(std::string server_config_path,
                  std::string client_config_path,
                  bool server) {
    LoadServerConfig(server_config_path);
    LoadClientConfig(client_config_path);
    LoadSharedMemory(server);
    queue_manager_.ClientInit(main_alloc_,
                              header_->queue_manager_,
                              header_->node_id_);
    if (!server) {
      HERMES_THREAD_MODEL->SetThreadModel(hshm::ThreadType::kPthread);
    }
  }

 public:
  /** Connect to a Daemon's shared memory */
  void LoadSharedMemory(bool server) {
    // Load shared-memory allocator
    config::QueueManagerInfo &qm = server_config_.queue_manager_;
    auto mem_mngr = HERMES_MEMORY_MANAGER;
    if (!server) {
      mem_mngr->AttachBackend(hipc::MemoryBackendType::kPosixShmMmap,
                              qm.shm_name_);
      mem_mngr->AttachBackend(hipc::MemoryBackendType::kPosixShmMmap,
                              qm.data_shm_name_);
      mem_mngr->AttachBackend(hipc::MemoryBackendType::kPosixShmMmap,
                              qm.rdata_shm_name_);
    }
    main_alloc_ = mem_mngr->GetAllocator(main_alloc_id_);
    data_alloc_ = mem_mngr->GetAllocator(data_alloc_id_);
    rdata_alloc_ = mem_mngr->GetAllocator(rdata_alloc_id_);
    header_ = main_alloc_->GetCustomHeader<HrunShm>();
    unique_ = &header_->unique_;
    node_id_ = header_->node_id_;
  }

  /** Finalize Hermes explicitly */
  void Finalize() {}

  /** Create task node id */
  TaskNode MakeTaskNodeId() {
    return TaskId(header_->node_id_, unique_->fetch_add(1));;
  }

  /** Create a unique ID */
  PoolId MakePoolId() {
    return PoolId(header_->node_id_, unique_->fetch_add(1));
  }

  /** Create a default-constructed task */
  template<typename TaskT, typename ...Args>
  HSHM_ALWAYS_INLINE
  TaskT* NewEmptyTask(hipc::Pointer &p) {
    TaskT *task = main_alloc_->NewObj<TaskT>(p, main_alloc_);
    if (task == nullptr) {
      // throw std::runtime_error("Could not allocate buffer");
      HELOG(kFatal, "Could not allocate buffer (1)");
    }
    return task;
  }

  /** Create a default-constructed task */
  template<typename TaskT, typename ...Args>
  HSHM_ALWAYS_INLINE
  LPointer<TaskT> NewEmptyTask() {
    LPointer<TaskT> task = main_alloc_->NewObjLocal<TaskT>(main_alloc_);
    if (task.shm_.IsNull()) {
      // throw std::runtime_error("Could not allocate buffer");
      HELOG(kFatal, "Could not allocate buffer (2)");
    }
    return task;
  }

  /** Construct task */
  template<typename TaskT, typename ...Args>
  HSHM_ALWAYS_INLINE
  void ConstructTask(TaskT *task, Args&& ...args) {
    return hipc::Allocator::ConstructObj<TaskT>(
        *task, main_alloc_, std::forward<Args>(args)...);
  }

  /** Allocate task */
  template<typename TaskT, typename ...Args>
  HSHM_ALWAYS_INLINE
      hipc::LPointer<TaskT> AllocateTask() {
    hipc::LPointer<TaskT> task = main_alloc_->AllocateLocalPtr<TaskT>(sizeof(TaskT));
    if (task.shm_.IsNull()) {
      HELOG(kFatal, "Could not allocate buffer (3)");
    }
    return task;
  }

  /** Create a task */
  template<typename TaskT, typename ...Args>
  HSHM_ALWAYS_INLINE
  LPointer<TaskT> NewTask(const TaskNode &task_node, Args&& ...args) {
    LPointer<TaskT> ptr = main_alloc_->NewObjLocal<TaskT>(
        main_alloc_, task_node, std::forward<Args>(args)...);
    if (ptr.shm_.IsNull()) {
      // throw std::runtime_error("Could not allocate buffer");
      HELOG(kFatal, "Could not allocate buffer (4)");
    }
    return ptr;
  }

  template<typename TaskT>
  void MonitorTaskFrees(LPointer<TaskT> &task) {
#ifdef CHIMAERA_TASK_DEBUG
    MonitorTaskFrees(task.ptr_);
#endif
  }

  void MonitorTaskFrees(Task *task) {
#ifdef CHIMAERA_TASK_DEBUG
    task->delcnt_++;
    if (task->delcnt_ != 1) {
      HELOG(kFatal, "Freed task {} times: node={}, state={}. method={}",
            task->delcnt_.load(), task->task_node_, task->pool_, task->method_)
    }
#endif
  }

  /** Destroy a task */
  template<typename TaskT>
  HSHM_ALWAYS_INLINE
  void DelTask(TaskT *task) {
#ifdef CHIMAERA_TASK_DEBUG
    MonitorTaskFrees(task);
#else
    main_alloc_->DelObj<TaskT>(task);
#endif
  }

  /** Destroy a task */
  template<typename TaskT>
  HSHM_ALWAYS_INLINE
  void DelTask(LPointer<TaskT> &task) {
#ifdef CHIMAERA_TASK_DEBUG
    MonitorTaskFrees(task);
#else
    main_alloc_->DelObjLocal<TaskT>(task);
#endif
  }

  /** Destroy a task */
  template<typename ContainerT, typename TaskT>
  HSHM_ALWAYS_INLINE
  void DelTask(ContainerT *exec, TaskT *task) {
#ifdef CHIMAERA_TASK_DEBUG
    MonitorTaskFrees(task);
#else
    exec->Del(task->method_, task);
#endif
  }

  /** Destroy a task */
  template<typename ContainerT, typename TaskT>
  HSHM_ALWAYS_INLINE
  void DelTask(ContainerT *exec, LPointer<TaskT> &task) {
#ifdef CHIMAERA_TASK_DEBUG
    MonitorTaskFrees(task);
#else
    exec->Del(task->method_, task);
#endif
  }

  /** Convert pointer to char* */
  template<typename T = char>
  HSHM_ALWAYS_INLINE
  T* GetMainPointer(const hipc::Pointer &p) {
    return main_alloc_->Convert<T, hipc::Pointer>(p);
  }

  /** Agnostic yield function */
  template<int THREAD_MODEL>
  HSHM_ALWAYS_INLINE
  void Yield() {
    if constexpr (THREAD_MODEL == TASK_YIELD_STD) {
      HERMES_THREAD_MODEL->Yield();
    } else if constexpr (THREAD_MODEL == TASK_YIELD_ABT) {
      ABT_thread_yield();
    }
  }

  /** Contextual yield function */
  template<int THREAD_MODEL>
  HSHM_ALWAYS_INLINE
  void Yield(Task *yield_task) {
    yield_task->Yield<THREAD_MODEL>();
  }

  /** Allocate a buffer */
  HSHM_ALWAYS_INLINE
  LPointer<char> AllocateBufferClient(size_t size) {
    return AllocateBufferSafe<TASK_YIELD_STD>(data_alloc_, size);
  }

  /** Allocate a buffer */
  template<int THREAD_MODEL>
  HSHM_ALWAYS_INLINE
  LPointer<char> AllocateBufferServer(size_t size) {
    return AllocateBufferSafe<THREAD_MODEL>(rdata_alloc_, size);
  }

  /** Allocate a buffer */
  template<int THREAD_MODEL>
  HSHM_ALWAYS_INLINE
  LPointer<char> AllocateBufferServer(size_t size, Task *yield_task) {
    return AllocateBufferSafe<THREAD_MODEL>(rdata_alloc_, size,
                                            yield_task);
  }

 private:
  /** Allocate a buffer */
  template<int THREAD_MODEL>
  HSHM_ALWAYS_INLINE
  LPointer<char> AllocateBufferSafe(Allocator *alloc, size_t size) {
    LPointer<char> p;
    while (true) {
      try {
        p = alloc->AllocateLocalPtr<char>(size);
      } catch (hshm::Error &e) {
        p.shm_.SetNull();
      }
      if (!p.shm_.IsNull()) {
        break;
      }
      Yield<THREAD_MODEL>();
    }
    return p;
  }

  /** Allocate a buffer */
  template<int THREAD_MODEL>
  HSHM_ALWAYS_INLINE
  LPointer<char> AllocateBufferSafe(Allocator *alloc, size_t size,
                                    Task *yield_task) {
    LPointer<char> p;
    while (true) {
      try {
        p = alloc->AllocateLocalPtr<char>(size);
      } catch (hshm::Error &e) {
        p.shm_.SetNull();
      }
      if (!p.shm_.IsNull()) {
        break;
      }
      Yield<THREAD_MODEL>(yield_task);
    }
    return p;
  }

 public:
  /** Free a buffer */
  HSHM_ALWAYS_INLINE
  void FreeBuffer(hipc::Pointer &p) {
    auto alloc = HERMES_MEMORY_MANAGER->GetAllocator(p.allocator_id_);
    alloc->Free(p);
  }

  /** Free a buffer */
  HSHM_ALWAYS_INLINE
  void FreeBuffer(LPointer<char> &p) {
    auto alloc = HERMES_MEMORY_MANAGER->GetAllocator(p.shm_.allocator_id_);
    alloc->FreeLocalPtr(p);
  }

  /** Convert pointer to char* */
  template<typename T = char>
  HSHM_ALWAYS_INLINE
  T* GetDataPointer(const hipc::Pointer &p) {
    auto alloc = HERMES_MEMORY_MANAGER->GetAllocator(p.allocator_id_);
    return alloc->Convert<T, hipc::Pointer>(p);
  }

  /** Get the queue ID */
  HSHM_ALWAYS_INLINE
  QueueId GetQueueId(const PoolId &id) {
    if (id == CHI_QM_CLIENT->process_queue_id_) {
      return CHI_QM_CLIENT->process_queue_id_;
    } else {
      return CHI_QM_CLIENT->admin_queue_id_;
    }
  }

  /** Get a queue by its ID */
  HSHM_ALWAYS_INLINE
  MultiQueue* GetQueue(const QueueId &queue_id) {
    QueueId real_id = GetQueueId(queue_id);
    return queue_manager_.GetQueue(real_id);
  }

  /** Schedule a task locally */
  template<typename TaskT>
  void ScheduleTaskRuntime(Task *parent_task,
                           LPointer<TaskT> &task,
                           const QueueId &queue_id) {
    std::vector<ResolvedDomainQuery> resolved =
        CHI_RPC->ResolveDomainQuery(task->pool_, task->dom_query_, false);
    MultiQueue *queue = GetQueue(queue_id);
    DomainQuery dom_query = resolved[0].dom_;
    if (resolved.size() == 1 && resolved[0].node_ == CHI_RPC->node_id_ &&
        dom_query.flags_.All(DomainQuery::kLocal | DomainQuery::kId)) {
      LaneGroup &lane_group = queue->GetGroup(task->prio_);
      u32 lane_id = dom_query.sel_.id_ % lane_group.num_lanes_;
      Lane &lane = lane_group.GetLane(lane_id);
      lane.emplace(task.shm_);
    } else {
      queue->Emplace(task->prio_,
                     std::hash<chi::DomainQuery>{}(task->dom_query_),
                     task.shm_);
    }
  }
};

/** The default asynchronous method behavior */
#define CHIMAERA_TASK_NODE_ROOT(CUSTOM)\
template<typename ...Args>\
hipc::LPointer<CUSTOM##Task> Async##CUSTOM##Alloc(const TaskNode &task_node,\
                                                  Args&& ...args) {\
  hipc::LPointer<CUSTOM##Task> task =\
    CHI_CLIENT->AllocateTask<CUSTOM##Task>();\
  Async##CUSTOM##Construct(task.ptr_, task_node, std::forward<Args>(args)...);\
  return task;\
}\
template<typename ...Args>\
hipc::LPointer<CUSTOM##Task> Async##CUSTOM(Task *parent_task,\
                                           const TaskNode &task_node,\
                                           Args&& ...args) {\
  hipc::LPointer<CUSTOM##Task> task = Async##CUSTOM##Alloc(\
    task_node, std::forward<Args>(args)...);\
  CHI_CLIENT->ScheduleTaskRuntime(parent_task, task, queue_id_);\
  return task;\
}\
template<typename ...Args>\
hipc::LPointer<CUSTOM##Task>\
Async##CUSTOM##Root(Args&& ...args) {\
  TaskNode task_node = CHI_CLIENT->MakeTaskNodeId();\
  hipc::LPointer<CUSTOM##Task> task = Async##CUSTOM##Alloc(\
      task_node, std::forward<Args>(args)...);\
  MultiQueue *queue = CHI_CLIENT->GetQueue(queue_id_);\
  queue->Emplace(TaskPrio::kLowLatency,\
                 std::hash<chi::DomainQuery>{}(task->dom_query_),\
                 task.shm_);\
  return task;\
}

#define CHI_TASK_METHODS(CUSTOM) CHIMAERA_TASK_NODE_ROOT(CUSTOM)

/** Call duplicate if applicable */
template<typename TaskT>
constexpr inline void CALL_COPY_START(const TaskT *orig_task,
                                      TaskT *dup_task,
                                      bool deep) {
  if constexpr (TaskT::REPLICA) {
    dup_task->task_dup(*orig_task);
    if (!deep) {
      dup_task->UnsetDataOwner();
    }
    dup_task->CopyStart(*orig_task, deep);
  }
}

/** Call duplicate if applicable */
template<typename TaskT>
constexpr inline void CALL_NEW_COPY_START(const TaskT *orig_task,
                                          LPointer<Task> &udup_task,
                                          bool deep) {
  if constexpr (TaskT::REPLICA) {
    LPointer<TaskT> dup_task = CHI_CLIENT->NewEmptyTask<TaskT>();
    CALL_COPY_START(orig_task, dup_task.ptr_, deep);
    udup_task.ptr_ = (Task *) dup_task.ptr_;
    udup_task.shm_ = dup_task.shm_;
  }
}

}  // namespace chi

static inline bool CHIMAERA_CLIENT_INIT() {
  if (!CHI_CLIENT->IsInitialized() &&
      !CHI_CLIENT->IsBeingInitialized() &&
      !CHI_CLIENT->IsTerminated()) {
    CHI_CLIENT->Create();
    CHI_CLIENT->is_transparent_ = true;
    return true;
  }
  return false;
}
#define TRANSPARENT_RUN CHIMAERA_CLIENT_INIT

#define HASH_TO_NODE_ID(hash) (1 + ((hash) % CHI_CLIENT->GetNumNodes()))

#endif  // HRUN_INCLUDE_CHI_CLIENT_CHI_CLIENT_H_
