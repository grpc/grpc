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

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "completion_queue.h"

#include "hphp/runtime/ext/extension.h"

namespace HPHP {

CompletionQueue::CompletionQueue(void)
{
    m_pCompletionQueue = grpc_completion_queue_create_for_pluck(nullptr);
}

CompletionQueue::~CompletionQueue(void)
{
    grpc_completion_queue_shutdown(m_pCompletionQueue);
    grpc_completion_queue_destroy(m_pCompletionQueue);
}

CompletionQueue& CompletionQueue::getQueue(void)
{
    // Completion queue per thread
    static CompletionQueue s_CompletionQueue;
    std::cout << "Completion Queue " << s_CompletionQueue.queue() << std::endl;
    return s_CompletionQueue;
}

}
