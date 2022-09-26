/*
 *
 * Copyright 2017 gRPC authors.
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

#include "test/cpp/microbenchmarks/helpers.h"

#include <string.h>

static grpc::internal::GrpcLibraryInitializer g_gli_initializer;
static LibraryInitializer* g_libraryInitializer;

LibraryInitializer::LibraryInitializer() {
  GPR_ASSERT(g_libraryInitializer == nullptr);
  g_libraryInitializer = this;

  g_gli_initializer.summon();
  init_lib_.init();
}

LibraryInitializer::~LibraryInitializer() {
  g_libraryInitializer = nullptr;
  init_lib_.shutdown();
}

LibraryInitializer& LibraryInitializer::get() {
  GPR_ASSERT(g_libraryInitializer != nullptr);
  return *g_libraryInitializer;
}
