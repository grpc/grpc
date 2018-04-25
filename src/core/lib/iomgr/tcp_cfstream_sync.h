/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPC_CORE_LIB_IOMGR_TCP_CFSTREAM_SYNC_H
#define GRPC_CORE_LIB_IOMGR_TCP_CFSTREAM_SYNC_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_CFSTREAM
#import <Foundation/Foundation.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/lockfree_event.h"

class CFStreamSync final {
 public:
  static CFStreamSync* CreateStreamSync(CFReadStreamRef read_stream,
                                        CFWriteStreamRef write_stream);
  ~CFStreamSync() {}
  CFStreamSync(const CFReadStreamRef& ref) = delete;
  CFStreamSync(CFReadStreamRef&& ref) = delete;
  CFStreamSync& operator=(const CFStreamSync& rhs) = delete;

  void NotifyOnOpen(grpc_closure* closure);
  void NotifyOnRead(grpc_closure* closure);
  void NotifyOnWrite(grpc_closure* closure);
  void Shutdown(grpc_error* error);

  void Ref(const char* file = nullptr, int line = 0,
           const char* reason = nullptr);
  void Unref(const char* file = nullptr, int line = 0,
             const char* reason = nullptr);

 private:
  CFStreamSync(CFReadStreamRef read_stream, CFWriteStreamRef write_stream);
  static void ReadCallback(CFReadStreamRef stream, CFStreamEventType type,
                           void* client_callback_info);
  static void WriteCallback(CFWriteStreamRef stream, CFStreamEventType type,
                            void* client_callback_info);
  static void* Retain(void* info);
  static void Release(void* info);

  grpc_core::LockfreeEvent open_event_;
  grpc_core::LockfreeEvent read_event_;
  grpc_core::LockfreeEvent write_event_;

  gpr_refcount refcount_;
};

#ifndef NDEBUG
#define CFSTREAM_SYNC_REF(sync, reason) \
  (sync)->Ref(__FILE__, __LINE__, (reason))
#define CFSTREAM_SYNC_UNREF(sync, reason) \
  (sync)->Unref(__FILE__, __LINE__, (reason))
#else
#define CFSTREAM_SYNC_REF(sync, reason) (sync)->Ref()
#define CFSTREAM_SYNC_UNREF(sync, reason) (sync)->Unref()
#endif

#endif

#endif /* GRPC_CORE_LIB_IOMGR_TCP_CFSTREAM_SYNC_H */
