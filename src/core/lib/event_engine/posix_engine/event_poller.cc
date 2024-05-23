// Copyright 2024 The gRPC Authors
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
#include "src/core/lib/event_engine/posix_engine/event_poller.h"

namespace grpc_event_engine {
namespace experimental {

//
// EventHandleRef
//
EventHandleRef::EventHandleRef(std::unique_ptr<EventHandle> event) {
  grpc_core::MutexLock lock(&mu_);
  if (event != nullptr) {
    event_ = std::move(event);
    event_->Poller()->RegisterEventHandleRef(this);
  }
}

EventHandleRef::~EventHandleRef() {
  grpc_core::MutexLock lock(&mu_);
  if (event_ != nullptr) {
    event_->Poller()->DeregisterEventHandleRef(this);
  }
}

EventHandleRef& EventHandleRef::operator=(std::unique_ptr<EventHandle> event) {
  grpc_core::MutexLock lock(&mu_);
  if (event_ != nullptr && event == nullptr) {
    event_->Poller()->DeregisterEventHandleRef(this);
  }
  std::swap(event_, event);
  if (event_ == nullptr && event != nullptr) {
    event_->Poller()->RegisterEventHandleRef(this);
  }
  return *this;
}

EventHandle* EventHandleRef::operator->() {
  grpc_core::MutexLock lock(&mu_);
  return event_.get();
}

std::unique_ptr<EventHandle> EventHandleRef::release() {
  grpc_core::MutexLock lock(&mu_);
  if (event_ != nullptr) {
    event_->Poller()->DeregisterEventHandleRef(this);
  }
  return std::move(event_);
}

EventHandle* EventHandleRef::get() {
  grpc_core::MutexLock lock(&mu_);
  return event_.get();
}

bool EventHandleRef::operator==(std::nullptr_t /* nullptr */) {
  grpc_core::MutexLock lock(&mu_);
  return event_ == nullptr;
}

bool EventHandleRef::operator!=(std::nullptr_t /* nullptr */) {
  grpc_core::MutexLock lock(&mu_);
  return event_ != nullptr;
}

std::unique_ptr<EventHandle> EventHandleRef::GiveUpEventHandleOnFork() {
  grpc_core::MutexLock lock(&mu_);
  return std::move(event_);
}

//
// EventHandleRefList
//
void EventHandleRefList::RegisterEventHandleRef(EventHandleRef* ref) {
  grpc_core::MutexLock lock(&mu_);
  refs_.emplace(ref);
}

void EventHandleRefList::DeregisterEventHandleRef(EventHandleRef* ref) {
  grpc_core::MutexLock lock(&mu_);
  refs_.erase(ref);
}

std::vector<std::unique_ptr<EventHandle>>
EventHandleRefList::ReleaseAllEvents() {
  std::vector<std::unique_ptr<EventHandle>> events;
  for (std::set<EventHandleRef*> refs = GetAllRefs(); !refs.empty();
       refs = GetAllRefs()) {
    events.reserve(events.size() + refs.size());
    for (EventHandleRef* ref : refs) {
      events.emplace_back(ref->GiveUpEventHandleOnFork());
    }
  }
  return events;
}

std::set<EventHandleRef*> EventHandleRefList::GetAllRefs() {
  grpc_core::MutexLock lock(&mu_);
  return std::move(refs_);
}

}  // namespace experimental
}  // namespace grpc_event_engine