//
// Created by llogan on 7/22/24.
//
#include "chimaera/module_registry/task.h"
#ifdef CHIMAERA_RUNTIME
#include "chimaera/work_orchestrator/work_orchestrator.h"
#include <thallium.hpp>
#endif

namespace chi {

void Task::YieldArgo() {
#ifdef CHIMAERA_RUNTIME
  ABT_thread_yield();
#endif
}

void Task::Wait(u32 flags) {
#ifdef CHIMAERA_RUNTIME
  Task *parent_task = CHI_CUR_TASK;
  if (this != parent_task) {
    parent_task->Wait(this, flags);
  } else {
    SpinWaitCo(flags);
  }
#else
  SpinWait(flags);
#endif
}

}  // namespace chi