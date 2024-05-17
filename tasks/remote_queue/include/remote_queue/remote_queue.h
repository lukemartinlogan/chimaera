//
// Created by lukemartinlogan on 6/29/23.
//

#ifndef CHI_REMOTE_QUEUE_H_
#define CHI_REMOTE_QUEUE_H_

#include "remote_queue_tasks.h"

namespace chi::remote_queue {

/**
 * Create remote_queue requests
 *
 * This is ONLY used in the Hermes runtime, and
 * should never be called in client programs!!!
 * */
class Client : public TaskLibClient {
 public:
  /** Default constructor */
  Client() = default;

  /** Destructor */
  ~Client() = default;

  /** Async create a task state */
  void AsyncCreateConstruct(CreateTask *task,
                            const TaskNode &task_node,
                            const DomainQuery &dom_query,
                            const DomainQuery &scope_query,
                            const std::string &state_name,
                            const CreateContext &ctx) {
    CHI_CLIENT->ConstructTask<CreateTask>(
        task, task_node, dom_query, scope_query, state_name, ctx);
  }
  void CreateRoot(const DomainQuery &dom_query,
                  const DomainQuery &scope_query,
                  const std::string &state_name,
                  const CreateContext &ctx = CreateContext()) {
    LPointer<CreateTask> task = AsyncCreateRoot(
        dom_query, scope_query, state_name, ctx);
    task->Wait();
    Init(task->ctx_.id_);
    CHI_CLIENT->DelTask(task);
  }
  CHI_TASK_METHODS(Create);

  /** Destroy task state + queue */
  HSHM_ALWAYS_INLINE
  void DestroyRoot(const DomainQuery &dom_query) {
    CHI_ADMIN->DestroyTaskStateRoot(dom_query, id_);
  }

  /** Construct submit aggregator */
  void AsyncClientPushSubmitConstruct(ClientPushSubmitTask *task,
                                      const TaskNode &task_node,
                                      const DomainQuery &dom_query,
                                      Task *orig_task) {
    CHI_CLIENT->ConstructTask<ClientPushSubmitTask>(
        task, task_node,
        dom_query, id_, orig_task);
  }
  CHI_TASK_METHODS(ClientPushSubmit)

  /** Construct submit aggregator */
  void AsyncClientSubmitConstruct(ClientSubmitTask *task,
                                  const TaskNode &task_node,
                                  const DomainQuery &dom_query) {
    CHI_CLIENT->ConstructTask<ClientSubmitTask>(
        task, task_node,
        dom_query, id_);
  }
  CHI_TASK_METHODS(ClientSubmit)

  /** Construct complete aggregator */
  void AsyncServerCompleteConstruct(ServerCompleteTask *task,
                                    const TaskNode &task_node,
                                    const DomainQuery &dom_query) {
    CHI_CLIENT->ConstructTask<ServerCompleteTask>(
        task, task_node, dom_query, id_);
  }
  CHI_TASK_METHODS(ServerComplete)
};

}  // namespace chi

#endif  // CHI_REMOTE_QUEUE_H_
