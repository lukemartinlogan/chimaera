#ifndef HRUN_TASKS_CHI_ADMIN_INCLUDE_CHI_ADMIN_CHI_ADMIN_TASKS_H_
#define HRUN_TASKS_CHI_ADMIN_INCLUDE_CHI_ADMIN_CHI_ADMIN_TASKS_H_

#include "chimaera/work_orchestrator/scheduler.h"
#include "chimaera/api/chimaera_client.h"
#include "chimaera/queue_manager/queue_manager_client.h"
#include "chimaera/chimaera_namespace.h"

namespace chi::Admin {

#include "chimaera_admin_methods.h"

/** A template to register or destroy a task library */
template<int method>
struct RegisterTaskLibTaskTempl : public Task, TaskFlags<TF_SRL_SYM> {
  IN hipc::string lib_name_;

  /** SHM default constructor */
  HSHM_ALWAYS_INLINE explicit
  RegisterTaskLibTaskTempl(hipc::Allocator *alloc)
  : Task(alloc), lib_name_(alloc) {}

  /** Emplace constructor */
  HSHM_ALWAYS_INLINE explicit
  RegisterTaskLibTaskTempl(hipc::Allocator *alloc,
                           const TaskNode &task_node,
                           const DomainQuery &dom_query,
                           const std::string &lib_name)
  : Task(alloc), lib_name_(alloc, lib_name) {
    // Initialize task
    task_node_ = task_node;
    prio_ = TaskPrio::kLowLatency;
    task_state_ = CHI_QM_CLIENT->admin_task_state_;
    if constexpr(method == 0) {
      method_ = Method::kRegisterTaskLib;
    } else {
      method_ = Method::kDestroyTaskLib;
    }
    task_flags_.SetBits(0);
    dom_query_ = dom_query;
  }

  /** Destructor */
  ~RegisterTaskLibTaskTempl() {}

  /** Duplicate message */
  void CopyStart(const RegisterTaskLibTaskTempl &other, bool deep) {
    lib_name_ = other.lib_name_;
  }

  /** (De)serialize message call */
  template<typename Ar>
  void SerializeStart(Ar &ar) {
    ar(lib_name_);
  }

  /** (De)serialize message return */
  template<typename Ar>
  void SerializeEnd(Ar &ar) {
  }
};

/** A task to register a Task Library */
using RegisterTaskLibTask = RegisterTaskLibTaskTempl<0>;

/** A task to destroy a Task Library */
using DestroyTaskLibTask = RegisterTaskLibTaskTempl<1>;

class CreateTaskStatePhase {
 public:
  // NOTE(llogan): kLast is intentially 0 so that the constructor
  // can seamlessly pass data to submethods
  TASK_METHOD_T kIdAllocStart = 0;
  TASK_METHOD_T kIdAllocWait = 1;
  TASK_METHOD_T kStateCreate = 2;
  TASK_METHOD_T kLast = 0;
};

/** A task to register a Task state + Create a queue */
struct CreateTaskStateTask : public Task, TaskFlags<TF_SRL_SYM> {
  IN hipc::string lib_name_;
  IN hipc::string state_name_;
  IN DomainQuery scope_query_;
  IN bool root_ = true;
  INOUT CreateContext ctx_;

  /** SHM default constructor */
  HSHM_ALWAYS_INLINE explicit
  CreateTaskStateTask(hipc::Allocator *alloc)
  : Task(alloc), lib_name_(alloc), state_name_(alloc) {
  }

  /** Emplace constructor */
  HSHM_ALWAYS_INLINE explicit
  CreateTaskStateTask(hipc::Allocator *alloc,
                      const TaskNode &task_node,
                      const DomainQuery &dom_query,
                      const DomainQuery &scope_query,
                      const std::string &state_name,
                      const std::string &lib_name,
                      const CreateContext &ctx)
  : Task(alloc),
    state_name_(alloc, state_name),
    lib_name_(alloc, lib_name) {
    // Initialize task
    task_node_ = task_node;
    prio_ = TaskPrio::kLowLatency;
    task_state_ = CHI_QM_CLIENT->admin_task_state_;
    method_ = Method::kCreateTaskState;
    task_flags_.SetBits(TASK_COROUTINE);
    dom_query_ = dom_query;

    // Initialize
    scope_query_ = scope_query;
    ctx_ = ctx;
  }

  /** Destructor */
  ~CreateTaskStateTask() {}

  /** Duplicate message */
  template<typename CreateTaskT = CreateTaskStateTask>
  void CopyStart(const CreateTaskT &other, bool deep) {
    lib_name_ = other.lib_name_;
    state_name_ = other.state_name_;
    ctx_ = other.ctx_;
    root_ = other.root_;
    scope_query_ = other.scope_query_;
    HILOG(kInfo, "Copying CreateTaskStateTask: {} {} {}",
          lib_name_.str(), state_name_.str(), ctx_.id_)
  }

  /** (De)serialize message call */
  template<typename Ar>
  void SerializeStart(Ar &ar) {
    ar(lib_name_, state_name_, ctx_, root_, scope_query_);
  }

  /** (De)serialize message return */
  template<typename Ar>
  void SerializeEnd(Ar &ar) {
    ar(ctx_.id_);
  }
};

/** A task to retrieve the ID of a task */
struct GetTaskStateIdTask : public Task, TaskFlags<TF_SRL_SYM> {
  IN hipc::string state_name_;
  OUT TaskStateId id_;

  /** SHM default constructor */
  HSHM_ALWAYS_INLINE explicit
  GetTaskStateIdTask(hipc::Allocator *alloc)
  : Task(alloc), state_name_(alloc) {}

  /** Emplace constructor */
  HSHM_ALWAYS_INLINE explicit
  GetTaskStateIdTask(hipc::Allocator *alloc,
                     const TaskNode &task_node,
                     const DomainQuery &dom_query,
                     const std::string &state_name)
  : Task(alloc), state_name_(alloc, state_name) {
    // Initialize task
    task_node_ = task_node;
    prio_ = TaskPrio::kLowLatency;
    task_state_ = CHI_QM_CLIENT->admin_task_state_;
    method_ = Method::kGetTaskStateId;
    task_flags_.SetBits(0);
    dom_query_ = dom_query; }

  ~GetTaskStateIdTask() {}

  /** Copy message input */
  void CopyStart(const GetTaskStateIdTask &other,
                 bool deep) {
    state_name_ = other.state_name_;
  }

  /** (De)serialize message call */
  template<typename Ar>
  void SerializeStart(Ar &ar) {
    ar(state_name_);
  }

  /** (De)serialize message return */
  template<typename Ar>
  void SerializeEnd(Ar &ar) {
    ar(id_);
  }
};

/** A task to destroy a Task state */
struct DestroyTaskStateTask : public Task, TaskFlags<TF_SRL_SYM> {
  IN TaskStateId id_;

  /** SHM default constructor */
  HSHM_ALWAYS_INLINE explicit
  DestroyTaskStateTask(hipc::Allocator *alloc) : Task(alloc) {}

  /** Emplace constructor */
  HSHM_ALWAYS_INLINE explicit
  DestroyTaskStateTask(hipc::Allocator *alloc,
                       const TaskNode &task_node,
                       const DomainQuery &dom_query,
                       const TaskStateId &id) : Task(alloc) {
    // Initialize task
    task_node_ = task_node;
    prio_ = TaskPrio::kLowLatency;
    task_state_ = CHI_QM_CLIENT->admin_task_state_;
    method_ = Method::kDestroyTaskState;
    task_flags_.SetBits(0);
    dom_query_ = dom_query;

    // Initialize
    id_ = id;
  }

  /** Copy message input */
  template<typename DestroyTaskT>
  void CopyStart(const DestroyTaskT &other,
                 bool deep) {
    id_ = other.id_;
  }

  /** (De)serialize message call */
  template<typename Ar>
  void SerializeStart(Ar &ar) {
    ar & id_;
  }

  /** (De)serialize message return */
  template<typename Ar>
  void SerializeEnd(Ar &ar) {
  }
};

/** A task to stop a runtime */
struct StopRuntimeTask : public Task, TaskFlags<TF_SRL_SYM> {
  /** SHM default constructor */
  StopRuntimeTask(hipc::Allocator *alloc) : Task(alloc) {}

  /** Emplace constructor */
  HSHM_ALWAYS_INLINE explicit
  StopRuntimeTask(hipc::Allocator *alloc,
                  const TaskNode &task_node,
                  const DomainQuery &dom_query) : Task(alloc) {
    // Initialize task
    task_node_ = task_node;
    prio_ = TaskPrio::kLowLatency;
    task_state_ = CHI_QM_CLIENT->admin_task_state_;
    method_ = Method::kStopRuntime;
    task_flags_.SetBits(TASK_FLUSH | TASK_FIRE_AND_FORGET);
    dom_query_ = dom_query;
  }

  /** Duplicate message */
  void CopyStart(StopRuntimeTask &other, bool deep) {
  }

  /** (De)serialize message call */
  template<typename Ar>
  void SerializeStart(Ar &ar) {
  }

  /** (De)serialize message return */
  template<typename Ar>
  void SerializeEnd(Ar &ar) {
  }
};

/** A task to set work orchestration policy */
template<int method>
struct SetWorkOrchestratorPolicyTask : public Task, TaskFlags<TF_SRL_SYM> {
  IN TaskStateId policy_id_;

  /** SHM default constructor */
  HSHM_ALWAYS_INLINE explicit
  SetWorkOrchestratorPolicyTask(hipc::Allocator *alloc) : Task(alloc) {}

  /** Emplace constructor */
  HSHM_ALWAYS_INLINE explicit
  SetWorkOrchestratorPolicyTask(hipc::Allocator *alloc,
                                const TaskNode &task_node,
                                const DomainQuery &dom_query,
                                const TaskStateId &policy_id) : Task(alloc) {
    // Initialize task
    task_node_ = task_node;
    prio_ = TaskPrio::kLowLatency;
    task_state_ = CHI_QM_CLIENT->admin_task_state_;
    if constexpr(method == 0) {
      method_ = Method::kSetWorkOrchQueuePolicy;
    } else {
      method_ = Method::kSetWorkOrchProcPolicy;
    }
    task_flags_.SetBits(0);
    dom_query_ = dom_query;

    // Initialize
    policy_id_ = policy_id;
  }

  /** Duplicate message */
  void CopyStart(SetWorkOrchestratorPolicyTask &other, bool deep) {
    policy_id_ = other.policy_id_;
  }

  /** (De)serialize message call */
  template<typename Ar>
  void SerializeStart(Ar &ar) {
    ar(policy_id_);
  }

  /** (De)serialize message return */
  template<typename Ar>
  void SerializeEnd(Ar &ar) {
  }
};
using SetWorkOrchQueuePolicyTask = SetWorkOrchestratorPolicyTask<0>;
using SetWorkOrchProcPolicyTask = SetWorkOrchestratorPolicyTask<1>;

/** A task to flush the runtime */
struct FlushTask : public Task, TaskFlags<TF_SRL_SYM> {
  INOUT size_t work_done_;

  /** SHM default constructor */
  FlushTask(hipc::Allocator *alloc) : Task(alloc) {}

  /** Emplace constructor */
  HSHM_ALWAYS_INLINE explicit
  FlushTask(hipc::Allocator *alloc,
            const TaskNode &task_node,
            const DomainQuery &dom_query) : Task(alloc) {
    // Initialize task
    task_node_ = task_node;
    prio_ = TaskPrio::kLowLatency;
    task_state_ = CHI_QM_CLIENT->admin_task_state_;
    method_ = Method::kFlush;
    task_flags_.SetBits(TASK_FLUSH | TASK_COROUTINE);
    dom_query_ = dom_query;

    // Custom
    work_done_ = 0;
  }

  /** Duplicate message */
  void CopyStart(FlushTask &other, bool deep) {
    work_done_ = other.work_done_;
  }

  /** (De)serialize message call */
  template<typename Ar>
  void SerializeStart(Ar &ar) {
    ar(work_done_);
  }

  /** (De)serialize message return */
  template<typename Ar>
  void SerializeEnd(Ar &ar) {
    ar(work_done_);
  }
};

/** A task to get the domain size */
struct GetDomainSizeTask : public Task, TaskFlags<TF_LOCAL> {
  IN DomainId dom_id_;
  OUT size_t dom_size_;

  /** SHM default constructor */
  GetDomainSizeTask(hipc::Allocator *alloc) : Task(alloc) {}

  /** Emplace constructor */
  HSHM_ALWAYS_INLINE explicit
  GetDomainSizeTask(hipc::Allocator *alloc,
                    const TaskNode &task_node,
                    const DomainQuery &dom_query,
                    const DomainId &dom_id) : Task(alloc) {
    // Initialize task
    task_node_ = task_node;
    prio_ = TaskPrio::kLowLatency;
    task_state_ = CHI_QM_CLIENT->admin_task_state_;
    method_ = Method::kGetDomainSize;
    task_flags_.SetBits(0);
    dom_query_ = dom_query;

    // Custom
    dom_id_ = dom_id;
    dom_size_ = 0;
  }
};

/** A task to update the lane mapping */
struct UpdateDomainTask : public Task, TaskFlags<TF_SRL_SYM> {
  IN hipc::vector<UpdateDomainInfo> ops_;

  /** SHM default constructor */
  UpdateDomainTask(hipc::Allocator *alloc)
  : Task(alloc), ops_(alloc) {}

  /** Emplace constructor */
  HSHM_ALWAYS_INLINE explicit
  UpdateDomainTask(
      hipc::Allocator *alloc,
      const TaskNode &task_node,
      const DomainQuery &dom_query,
      const TaskStateId &state_id,
      const std::vector<UpdateDomainInfo> &ops)
  : Task(alloc), ops_(alloc) {
    // Initialize task
    task_node_ = task_node;
    prio_ = TaskPrio::kLowLatency;
    task_state_ = CHI_QM_CLIENT->admin_task_state_;
    method_ = Method::kUpdateDomain;
    task_flags_.SetBits(0);
    dom_query_ = dom_query;

    // Custom
    ops_ = ops;
  }

  /** Duplicate message */
  void CopyStart(UpdateDomainTask &other, bool deep) {
    ops_ = other.ops_;
  }

  /** (De)serialize message call */
  template<typename Ar>
  void SerializeStart(Ar &ar) {
    ar(ops_);
  }

  /** (De)serialize message return */
  template<typename Ar>
  void SerializeEnd(Ar &ar) {
  }
};

}  // namespace chi::Admin

#endif  // HRUN_TASKS_CHI_ADMIN_INCLUDE_CHI_ADMIN_CHI_ADMIN_TASKS_H_
