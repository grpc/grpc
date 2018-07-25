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

#include "test/cpp/util/subprocess.h"

#include <vector>

#include "test/core/util/subprocess.h"

namespace grpc {

static gpr_subprocess* MakeProcess(const std::vector<std::string>& args) {
  std::vector<const char*> vargs;
  for (auto it = args.begin(); it != args.end(); ++it) {
    vargs.push_back(it->c_str());
  }
  return gpr_subprocess_create(vargs.size(), &vargs[0]);
}

SubProcess::SubProcess(const std::vector<std::string>& args)
    : subprocess_(MakeProcess(args)) {}

SubProcess::~SubProcess() { gpr_subprocess_destroy(subprocess_); }

int SubProcess::Join() { return gpr_subprocess_join(subprocess_); }

void SubProcess::Interrupt() { gpr_subprocess_interrupt(subprocess_); }

}  // namespace grpc
