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

#ifndef HRUN_INCLUDE_HRUN_HRUN_NAMESPACE_H_
#define HRUN_INCLUDE_HRUN_HRUN_NAMESPACE_H_

#include "chimaera/api/chimaera_client.h"
#include "chimaera/task_registry/task_lib.h"

using chi::TaskMethod;
using chi::BinaryOutputArchive;
using chi::BinaryInputArchive;
using chi::Task;
using chi::TaskPointer;
using chi::MultiQueue;
using chi::PriorityInfo;
using chi::TaskNode;
using chi::DomainQuery;
using chi::PoolId;
using chi::QueueId;
using chi::TaskFlags;
using chi::DataTransfer;
using chi::SegmentedTransfer;
using chi::TaskLib;
using chi::TaskLibClient;
using chi::config::QueueManagerInfo;
using chi::TaskPrio;
using chi::RunContext;

using hshm::RwLock;
using hshm::Mutex;
using hshm::bitfield;
using hshm::bitfield8_t;
using hshm::bitfield16_t;
using hshm::bitfield32_t;
typedef hshm::bitfield<uint64_t> bitfield64_t;
using hshm::ScopedRwReadLock;
using hshm::ScopedRwWriteLock;
using hipc::LPointer;

#endif  // HRUN_INCLUDE_HRUN_HRUN_NAMESPACE_H_
