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

#ifndef GRPC_CORE_LIB_GPRPP_FORK_H
#define GRPC_CORE_LIB_GPRPP_FORK_H

/*
 * NOTE: FORKING IS NOT GENERALLY SUPPORTED, THIS IS ONLY INTENDED TO WORK
 *       AROUND VERY SPECIFIC USE CASES.
 */

#ifdef GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
#define GRPC_GET_FORK_EPOCH(fd) fd->fork_epoch
#define GRPC_SET_FORK_EPOCH(fd, val) fd->fork_epoch = val
#else
#define GRPC_GET_FORK_EPOCH(fd) 0
#define GRPC_SET_FORK_EPOCH(fd, val) \
  do {                               \
  } while (0)
#endif  // GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

namespace grpc_core {

namespace internal {
class ExecCtxState;
class ThreadState;
}  // namespace internal

class Fork {
 public:
  typedef void (*child_postfork_func)(void);

  static void GlobalInit();
  static void GlobalShutdown();

  // Returns true if fork suppport is enabled, false otherwise
  static bool Enabled();

  // Increment the count of active ExecCtxs.
  // Will block until a pending fork is complete if one is in progress.
  static void IncExecCtxCount();

  // Decrement the count of active ExecCtxs
  static void DecExecCtxCount();

  static void IncrementForkEpoch();
  static int GetForkEpoch();
  // Provide a function that will be invoked in the child's postfork handler to
  // shutdown inherited connections.
  static void SetShutdownChildConnectionsFunc(
      child_postfork_func shutdown_child_connections);
  static child_postfork_func GetShutdownChildConnectionsFunc();
  // Provide a function that will be invoked in the child's postfork handler to
  // reset the polling engine's internal state.
  static void SetResetChildPollingEngineFunc(
      child_postfork_func reset_child_polling_engine);
  static child_postfork_func GetResetChildPollingEngineFunc();

  // Check if there is a single active ExecCtx
  // (the one used to invoke this function).  If there are more,
  // return false.  Otherwise, return true and block creation of
  // more ExecCtx s until AlloWExecCtx() is called
  //
  static bool BlockExecCtx();
  static void AllowExecCtx();

  // Increment the count of active threads.
  static void IncThreadCount();

  // Decrement the count of active threads.
  static void DecThreadCount();

  // Await all core threads to be joined.
  static void AwaitThreads();

  // Test only: overrides environment variables/compile flags
  // Must be called before grpc_init()
  static void Enable(bool enable);

 private:
  static internal::ExecCtxState* exec_ctx_state_;
  static internal::ThreadState* thread_state_;
  static bool support_enabled_;
  static bool override_enabled_;
  static int fork_epoch_;
  static child_postfork_func shutdown_child_connections_;
  static child_postfork_func reset_child_polling_engine_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_FORK_H */
