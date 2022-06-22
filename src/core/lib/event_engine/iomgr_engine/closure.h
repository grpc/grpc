// Copyright 2022 The gRPC Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_CLOSURE_H
#define GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_CLOSURE_H
#include <grpc/support/port_platform.h>

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/atm.h>

namespace grpc_event_engine {
namespace iomgr_engine {

class IomgrEngineClosure : public experimental::EventEngine::Closure {
 public:
  IomgrEngineClosure(std::function<void(absl::Status)>&& cb) cb_(std::move(cb)),
      status_(absl::OkStatus()){};
  ~IomgrEngineClosure() = default;
  void SetStatus(absl::Status status) { status_ = status; }
  void Run() override { cb_(absl::exchange(status_, absl::OkStatus())); }

 private:
  std::function<void(absl::Status)> cb_;
  absl::Status status_;
}

experimental::EventEngine::Closure*
MakeIomgrEngineClosure(std::function<void(absl::Status)>&& cb) {
  return new IomgrEngineClosure(std::move(cb));
}

}  // namespace iomgr_engine
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_IOMGR_ENGINE_CLOSURE_H