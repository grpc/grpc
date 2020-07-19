/*
 *
 * Copyright 2015-2016 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <stdlib.h>

#include <vector>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/shutdown_library.h"

struct ShutdownData {
  ~ShutdownData() {
    std::reverse(functions.begin(), functions.end());
    for (auto pair : functions) pair.first(pair.second);
  }

  static ShutdownData* get() {
    static auto* data = new ShutdownData;
    return data;
  }

  std::vector<std::pair<void (*)(const void*), const void*>> functions;
  grpc_core::Mutex mutex;
};

static void ZeroArgFuncWrapper(const void* arg) {
  void (*func)() = reinterpret_cast<void (*)()>(const_cast<void*>(arg));
  func();
}

void grpc_on_shutdown_callback(void (*func)()) {
  grpc_on_shutdown_callback_with_arg(ZeroArgFuncWrapper,
                                     reinterpret_cast<void*>(func));
}

void grpc_on_shutdown_callback_with_arg(void (*f)(const void*),
                                        const void* arg) {
  auto shutdown_data = ShutdownData::get();
  grpc_core::MutexLock lock(&shutdown_data->mutex);
  shutdown_data->functions.push_back(std::make_pair(f, arg));
}

void grpc_final_shutdown_library( void ) {
  GRPC_API_TRACE("grpc_final_shutdown_library(void)", 0, ());

  // Executes all custom cleanup functions, which were registered via
  // grpc_on_shutdown_callback() and grpc_on_shutdown_callback_with_arg()
  if (!grpc_is_initialized())
    delete ShutdownData::get();
}
