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

#include <grpc/support/port_platform.h>
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_CFSTREAM
#import <CoreFoundation/CoreFoundation.h>
#import "src/core/lib/iomgr/tcp_cfstream_sync.h"

#include <grpc/support/atm.h>
#include <grpc/support/sync.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

void* CFStreamSync::Retain(void* info) {
  CFStreamSync* sync = static_cast<CFStreamSync*>(info);
  CFSTREAM_SYNC_REF(sync, "retain");
  return info;
}

void CFStreamSync::Release(void* info) {
  CFStreamSync* sync = static_cast<CFStreamSync*>(info);
  CFSTREAM_SYNC_UNREF(sync, "release");
}

CFStreamSync* CFStreamSync::CreateStreamSync(CFReadStreamRef read_stream,
                                             CFWriteStreamRef write_stream) {
  return new CFStreamSync(read_stream, write_stream);
}

void CFStreamSync::ReadCallback(CFReadStreamRef stream, CFStreamEventType type,
                                void* client_callback_info) {
  CFStreamSync* sync = static_cast<CFStreamSync*>(client_callback_info);
  CFSTREAM_SYNC_REF(sync, "read callback");
  dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
    grpc_core::ExecCtx exec_ctx;
    if (grpc_tcp_trace.enabled()) {
      gpr_log(GPR_DEBUG, "TCP ReadCallback (%p, %lu, %p)", stream, type, client_callback_info);
    }
    switch (type) {
      case kCFStreamEventOpenCompleted:
        sync->open_event_.SetReady();
        break;
      case kCFStreamEventHasBytesAvailable:
      case kCFStreamEventEndEncountered:
        sync->read_event_.SetReady();
        break;
      case kCFStreamEventErrorOccurred:
        sync->open_event_.SetReady();
        sync->read_event_.SetReady();
        break;
      default:
        // Impossible
        abort();
    }
    CFSTREAM_SYNC_UNREF(sync, "read callback");
  });
}
void CFStreamSync::WriteCallback(CFWriteStreamRef stream, CFStreamEventType type,
                                 void* clientCallBackInfo) {
  CFStreamSync* sync = static_cast<CFStreamSync*>(clientCallBackInfo);
  CFSTREAM_SYNC_REF(sync, "write callback");
  dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
    grpc_core::ExecCtx exec_ctx;
    if (grpc_tcp_trace.enabled()) {
      gpr_log(GPR_DEBUG, "TCP WriteCallback (%p, %lu, %p)", stream, type, clientCallBackInfo);
    }
    switch (type) {
      case kCFStreamEventOpenCompleted:
        sync->open_event_.SetReady();
        break;
      case kCFStreamEventCanAcceptBytes:
      case kCFStreamEventEndEncountered:
        sync->write_event_.SetReady();
        break;
      case kCFStreamEventErrorOccurred:
        sync->open_event_.SetReady();
        sync->write_event_.SetReady();
        break;
      default:
        // Impossible
        abort();
    }
    CFSTREAM_SYNC_UNREF(sync, "write callback");
  });
}

CFStreamSync::CFStreamSync(CFReadStreamRef read_stream, CFWriteStreamRef write_stream) {
  gpr_ref_init(&refcount_, 1);
  open_event_.InitEvent();
  read_event_.InitEvent();
  write_event_.InitEvent();
  CFStreamClientContext ctx = {0, static_cast<void*>(this), nil, nil, nil};
  CFReadStreamSetClient(read_stream,
                        kCFStreamEventOpenCompleted | kCFStreamEventHasBytesAvailable |
                            kCFStreamEventErrorOccurred | kCFStreamEventEndEncountered,
                        CFStreamSync::ReadCallback, &ctx);
  CFWriteStreamSetClient(write_stream,
                         kCFStreamEventOpenCompleted | kCFStreamEventCanAcceptBytes |
                             kCFStreamEventErrorOccurred | kCFStreamEventEndEncountered,
                         CFStreamSync::WriteCallback, &ctx);
  CFReadStreamScheduleWithRunLoop(read_stream, CFRunLoopGetMain(), kCFRunLoopCommonModes);
  CFWriteStreamScheduleWithRunLoop(write_stream, CFRunLoopGetMain(), kCFRunLoopCommonModes);
}

CFStreamSync::~CFStreamSync() {
  open_event_.DestroyEvent();
  read_event_.DestroyEvent();
  write_event_.DestroyEvent();
}

void CFStreamSync::NotifyOnOpen(grpc_closure* closure) { open_event_.NotifyOn(closure); }

void CFStreamSync::NotifyOnRead(grpc_closure* closure) { read_event_.NotifyOn(closure); }

void CFStreamSync::NotifyOnWrite(grpc_closure* closure) { write_event_.NotifyOn(closure); }

void CFStreamSync::Shutdown(grpc_error* error) {
  open_event_.SetShutdown(GRPC_ERROR_REF(error));
  read_event_.SetShutdown(GRPC_ERROR_REF(error));
  write_event_.SetShutdown(GRPC_ERROR_REF(error));
  GRPC_ERROR_UNREF(error);
}

void CFStreamSync::Ref(const char* file, int line, const char* reason) {
  if (grpc_tcp_trace.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&refcount_.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "TCP SYNC ref %p : %s %" PRIdPTR " -> %" PRIdPTR,
            this, reason, val, val + 1);
  }
  gpr_ref(&refcount_);
}

void CFStreamSync::Unref(const char* file, int line, const char* reason) {
  if (grpc_tcp_trace.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&refcount_.count);
    gpr_log(GPR_ERROR, "TCP SYNC unref %p : %s %" PRIdPTR " -> %" PRIdPTR, this, reason, val,
            val - 1);
  }
  if (gpr_unref(&refcount_)) {
    delete this;
  }
}

#endif
