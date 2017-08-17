/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <memory>

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "completion_queue.h"
#include "common.h"

#include "hphp/runtime/ext/extension.h"

namespace HPHP {

CompletionQueue::CompletionQueue(void)
{
    m_pCompletionQueue = grpc_completion_queue_create_for_pluck(nullptr);
}

CompletionQueue::~CompletionQueue(void)
{
    // queue must be destroyed after server

    // shutdown queue
    grpc_completion_queue_shutdown(m_pCompletionQueue);

    // destroy queue
    grpc_completion_queue_destroy(m_pCompletionQueue);
}

std::unique_ptr<CompletionQueue> CompletionQueue::getClientQueue(void)
{
    // Each client gets a completion queue for the thread it is running in
    return std::unique_ptr<CompletionQueue>{ new CompletionQueue{} };
}

std::unique_ptr<CompletionQueue> CompletionQueue::getServerQueue(void)
{
    // Each server gets a completion queue for the thread it is running in
    return std::unique_ptr<CompletionQueue>{ new CompletionQueue{} };
}


}
