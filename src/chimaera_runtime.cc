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

#include "hermes_shm/util/singleton.h"
#include "chimaera/api/chimaera_runtime.h"
#include "chimaera/module_registry/task.h"

namespace chi {

/** Create the server-side API */
Runtime* Runtime::Create(std::string server_config_path) {
  hshm::ScopedMutex lock(lock_, 1);
  if (is_initialized_) {
    return this;
  }
  is_being_initialized_ = true;
  ServerInit(std::move(server_config_path));
  is_initialized_ = true;
  is_being_initialized_ = false;
  return this;
}

/** Initialize */
void Runtime::ServerInit(std::string server_config_path) {
  LoadServerConfig(server_config_path);
  // HILOG(kInfo, "Initializing shared memory")
  InitSharedMemory();
  // HILOG(kInfo, "Initializing RPC")
  CHI_RPC->ServerInit(&server_config_);
  // HILOG(kInfo, "Initializing thallium")
  thallium_.ServerInit(CHI_RPC);
  // HILOG(kInfo, "Initializing queues + workers")
  header_->node_id_ = CHI_RPC->node_id_;
  header_->unique_ = 0;
  header_->num_nodes_ = server_config_.rpc_.host_names_.size();
  // Create module registry
  CHI_MOD_REGISTRY->ServerInit(&server_config_,
                                CHI_RPC->node_id_, header_->unique_);
  CHI_MOD_REGISTRY->RegisterModule("chimaera_admin");
  CHI_MOD_REGISTRY->RegisterModule("worch_queue_round_robin");
  CHI_MOD_REGISTRY->RegisterModule("worch_proc_round_robin");
  CHI_MOD_REGISTRY->RegisterModule("remote_queue");
  CHI_MOD_REGISTRY->RegisterModule("bdev");
  // Queue manager + client must be initialized before Work Orchestrator
  CHI_QM_RUNTIME->ServerInit(main_alloc_,
                            CHI_RPC->node_id_,
                            &server_config_,
                            header_->queue_manager_);
  CHI_CLIENT->Create(server_config_path, "", true);
  HERMES_THREAD_MODEL->SetThreadModel(hshm::ThreadType::kArgobots);
  CHI_WORK_ORCHESTRATOR->ServerInit(&server_config_, *CHI_QM_RUNTIME);
  hipc::mptr<Admin::CreateTask> admin_create_task;
  hipc::mptr<Admin::CreateContainerTask> create_task;
  u32 max_containers_pn = CHI_QM_RUNTIME->max_containers_pn_;
  size_t max_workers = server_config_.wo_.max_dworkers_ +
                       server_config_.wo_.max_oworkers_;
  std::vector<UpdateDomainInfo> ops;
  std::vector<SubDomainId> containers;

  // Create the admin library
  CHI_CLIENT->MakePoolId();
  admin_create_task = hipc::make_mptr<Admin::CreateTask>();
  ops = CHI_RPC->CreateDefaultDomains(
      CHI_QM_CLIENT->admin_pool_id_,
      CHI_QM_CLIENT->admin_pool_id_,
      DomainQuery::GetGlobal(chi::SubDomainId::kContainerSet, 0),
      CHI_RPC->hosts_.size(), 1);
  CHI_RPC->UpdateDomains(ops);
  containers =
      CHI_RPC->GetLocalContainers(CHI_QM_CLIENT->admin_pool_id_);
  CHI_MOD_REGISTRY->CreateContainer(
      "chimaera_admin",
      "chimaera_admin",
      CHI_QM_CLIENT->admin_pool_id_,
      admin_create_task.get(),
      containers);

  // Create the work orchestrator queue scheduling library
  PoolId queue_sched_id = CHI_CLIENT->MakePoolId();
  create_task = hipc::make_mptr<Admin::CreateContainerTask>();
  ops = CHI_RPC->CreateDefaultDomains(
      queue_sched_id,
      CHI_QM_CLIENT->admin_pool_id_,
      DomainQuery::GetGlobal(chi::SubDomainId::kLocalContainers, 0),
      1, 1);
  CHI_RPC->UpdateDomains(ops);
  containers = CHI_RPC->GetLocalContainers(queue_sched_id);
  CHI_MOD_REGISTRY->CreateContainer(
      "worch_queue_round_robin",
      "worch_queue_round_robin",
      queue_sched_id,
      create_task.get(),
      containers);

  // Create the work orchestrator process scheduling library
  PoolId proc_sched_id = CHI_CLIENT->MakePoolId();
  create_task = hipc::make_mptr<Admin::CreateContainerTask>();
  ops = CHI_RPC->CreateDefaultDomains(
      proc_sched_id,
      CHI_QM_CLIENT->admin_pool_id_,
      DomainQuery::GetGlobal(chi::SubDomainId::kLocalContainers, 0),
      1, 1);
  CHI_RPC->UpdateDomains(ops);
  containers = CHI_RPC->GetLocalContainers(proc_sched_id);
  CHI_MOD_REGISTRY->CreateContainer(
      "worch_proc_round_robin",
      "worch_proc_round_robin",
      proc_sched_id,
      create_task.get(),
      containers);

  // Set the work orchestrator queue scheduler
  CHI_ADMIN->SetWorkOrchQueuePolicyRN(
      DomainQuery::GetDirectHash(chi::SubDomainId::kLocalContainers, 0),
      queue_sched_id);
  CHI_ADMIN->SetWorkOrchProcPolicyRN(
      DomainQuery::GetDirectHash(chi::SubDomainId::kLocalContainers, 0),
      proc_sched_id);

  // Create the remote queue library
  remote_queue_.Create(
      DomainQuery::GetDirectHash(chi::SubDomainId::kLocalContainers, 0),
      DomainQuery::GetDirectHash(chi::SubDomainId::kLocalContainers, 0),
      "remote_queue",
      CreateContext{CHI_CLIENT->MakePoolId(), 1, max_containers_pn});
  remote_created_ = true;
}

/** Initialize shared-memory between daemon and client */
void Runtime::InitSharedMemory() {
  // Create shared-memory allocator
  config::QueueManagerInfo &qm = server_config_.queue_manager_;
  auto mem_mngr = HERMES_MEMORY_MANAGER;
  if (qm.shm_size_ == 0) {
    qm.shm_size_ =
        hipc::MemoryManager::GetDefaultBackendSize();
  }
  // Create general allocator
  mem_mngr->CreateBackend<hipc::PosixShmMmap>(
      qm.shm_size_,
      qm.shm_name_);
  main_alloc_ =
      mem_mngr->CreateAllocator<hipc::ScalablePageAllocator>(
          qm.shm_name_,
          main_alloc_id_,
          sizeof(ChiShm));
  header_ = main_alloc_->GetCustomHeader<ChiShm>();
  // Create separate data allocator
  mem_mngr->CreateBackend<hipc::PosixShmMmap>(
      qm.data_shm_size_,
      qm.data_shm_name_);
  data_alloc_ =
      mem_mngr->CreateAllocator<hipc::ScalablePageAllocator>(
          qm.data_shm_name_,
          data_alloc_id_, 0);
  // Create separate runtime data allocator
  mem_mngr->CreateBackend<hipc::PosixShmMmap>(
      qm.rdata_shm_size_,
      qm.rdata_shm_name_);
  rdata_alloc_ =
      mem_mngr->CreateAllocator<hipc::ScalablePageAllocator>(
          qm.rdata_shm_name_,
          rdata_alloc_id_, 0);
}

/** Finalize Hermes explicitly */
void Runtime::Finalize() {
}

/** Run the Hermes core Daemon */
void Runtime::RunDaemon() {
  thallium_.RunDaemon();
  HILOG(kInfo, "(node {}) Finishing up last requests",
        CHI_CLIENT->node_id_)
  CHI_WORK_ORCHESTRATOR->Join();
  HILOG(kInfo, "(node {}) Daemon is exiting",
        CHI_CLIENT->node_id_)
}

/** Stop the Hermes core Daemon */
void Runtime::StopDaemon() {
  CHI_WORK_ORCHESTRATOR->FinalizeRuntime();
}

}  // namespace chi

/** Runtime singleton */
DEFINE_SINGLETON_CC(chi::Runtime)
DEFINE_SINGLETON_CC(chi::RpcContext)
DEFINE_SINGLETON_CC(chi::WorkOrchestrator)
DEFINE_SINGLETON_CC(chi::ModuleRegistry)
DEFINE_SINGLETON_CC(chi::QueueManagerRuntime)
