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

    // Wait for confirmation of queue shutdown
    for(;;)
    {
        grpc_event event( grpc_completion_queue_pluck(m_pCompletionQueue, nullptr,
                                                      gpr_time_from_millis(100, GPR_TIMESPAN),
                                                      nullptr) );
        if (event.type == GRPC_QUEUE_SHUTDOWN ||
            event.type == GRPC_QUEUE_TIMEOUT) break;
    }

    // destroy queue.  This will segfault if there are still pending items in queue
    grpc_completion_queue_destroy(m_pCompletionQueue);
}

void CompletionQueue::getClientQueue(std::unique_ptr<CompletionQueue>& pCompletionQueue)
{
    // Each client gets a completion queue
    static std::mutex queueMutex;
    {
        std::unique_lock<std::mutex> lock{ queueMutex };
        pCompletionQueue.reset(new CompletionQueue{});
    }
}

void CompletionQueue::getServerQueue(std::unique_ptr<CompletionQueue>& pCompletionQueue)
{
    // Each server gets a completion queue
    static std::mutex queueMutex;
    {
        std::unique_lock<std::mutex> lock{ queueMutex };
        pCompletionQueue.reset(new CompletionQueue{});
    }
}


}
