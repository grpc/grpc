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

#ifndef GRPC_CORE_LIB_GPRPP_THD_H
#define GRPC_CORE_LIB_GPRPP_THD_H

/** Internal thread interface. */

#include <grpc/support/port_platform.h>

#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd_id.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/abstract.h"
#include "src/core/lib/gprpp/memory.h"

namespace grpc_core {
namespace internal {

/// Base class for platform-specific thread-state
class ThreadInternalsInterface {
 public:
  virtual ~ThreadInternalsInterface() {}
  virtual void Start() GRPC_ABSTRACT;
  virtual void Join() GRPC_ABSTRACT;
  GRPC_ABSTRACT_BASE_CLASS
};

}  // namespace internal

class Thread {
 public:
  /// Default constructor only to allow use in structs that lack constructors
  /// Does not produce a validly-constructed thread; must later
  /// use placement new to construct a real thread. Does not init mu_ and cv_
  Thread() : state_(FAKE), impl_(nullptr) {}

  /// Normal constructor to create a thread with name \a thd_name,
  /// which will execute a thread based on function \a thd_body
  /// with argument \a arg once it is started.
  /// The optional \a success argument indicates whether the thread
  /// is successfully created.
  Thread(const char* thd_name, void (*thd_body)(void* arg), void* arg,
         bool* success = nullptr);

  /// Move constructor for thread. After this is called, the other thread
  /// no longer represents a living thread object
  Thread(Thread&& other) : state_(other.state_), impl_(other.impl_) {
    other.state_ = MOVED;
    other.impl_ = nullptr;
  }

  /// Move assignment operator for thread. After this is called, the other
  /// thread no longer represents a living thread object. Not allowed if this
  /// thread actually exists
  Thread& operator=(Thread&& other) {
    if (this != &other) {
      // TODO(vjpai): if we can be sure that all Thread's are actually
      // constructed, then we should assert GPR_ASSERT(impl_ == nullptr) here.
      // However, as long as threads come in structures that are
      // allocated via gpr_malloc, this will not be the case, so we cannot
      // assert it for the time being.
      state_ = other.state_;
      impl_ = other.impl_;
      other.state_ = MOVED;
      other.impl_ = nullptr;
    }
    return *this;
  }

  /// The destructor is strictly optional; either the thread never came to life
  /// and the constructor itself killed it or it has already been joined and
  /// the Join function kills it. The destructor shouldn't have to do anything.
  ~Thread() { GPR_ASSERT(impl_ == nullptr); }

  void Start() {
    if (impl_ != nullptr) {
      GPR_ASSERT(state_ == ALIVE);
      state_ = STARTED;
      impl_->Start();
    } else {
      GPR_ASSERT(state_ == FAILED);
    }
  };

  void Join() {
    if (impl_ != nullptr) {
      impl_->Join();
      grpc_core::Delete(impl_);
      state_ = DONE;
      impl_ = nullptr;
    } else {
      GPR_ASSERT(state_ == FAILED);
    }
  };

 private:
  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;

  /// The thread states are as follows:
  /// FAKE -- just a dummy placeholder Thread created by the default constructor
  /// ALIVE -- an actual thread of control exists associated with this thread
  /// STARTED -- the thread of control has been started
  /// DONE -- the thread of control has completed and been joined
  /// FAILED -- the thread of control never came alive
  /// MOVED -- contents were moved out and we're no longer tracking them
  enum ThreadState { FAKE, ALIVE, STARTED, DONE, FAILED, MOVED };
  ThreadState state_;
  internal::ThreadInternalsInterface* impl_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_THD_H */
