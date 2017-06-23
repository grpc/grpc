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

#include "completion_queue.h"

// TODO: This struct and related functions appread to be used in thread safe ways
// But someone should double check this
grpc_completion_queue *completion_queue;

void grpc_hhvm_init_completion_queue() {
  completion_queue = grpc_completion_queue_create_for_pluck(NULL);
}

void grpc_hhvm_shutdown_completion_queue() {
  grpc_completion_queue_shutdown(completion_queue);
  grpc_completion_queue_destroy(completion_queue);
}
