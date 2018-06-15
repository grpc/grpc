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
#import "src/core/lib/iomgr/cfstream_handle.h"

#include <grpc/support/atm.h>
#include <grpc/support/sync.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

void* CFStreamHandle::Retain(void* info) {
  CFStreamHandle* handle = static_cast<CFStreamHandle*>(info);
  CFSTREAM_HANDLE_REF(handle, "retain");
  return info;
}

void CFStreamHandle::Release(void* info) {
  CFStreamHandle* handle = static_cast<CFStreamHandle*>(info);
  CFSTREAM_HANDLE_UNREF(handle, "release");
}

CFStreamHandle* CFStreamHandle::CreateStreamHandle(
    CFReadStreamRef read_stream, CFWriteStreamRef write_stream) {
  return new CFStreamHandle(read_stream, write_stream);
}

void CFStreamHandle::ReadCallback(CFReadStreamRef stream,
                                  CFStreamEventType type,
                                  void* client_callback_info) {
  CFStreamHandle* handle = static_cast<CFStreamHandle*>(client_callback_info);
  CFSTREAM_HANDLE_REF(handle, "read callback");
  dispatch_async(
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        grpc_core::ExecCtx exec_ctx;
        if (grpc_tcp_trace.enabled()) {
          gpr_log(GPR_DEBUG, "CFStream ReadCallback (%p, %p, %lu, %p)", handle,
                  stream, type, client_callback_info);
        }
        switch (type) {
          case kCFStreamEventOpenCompleted:
            handle->open_event_.SetReady();
            break;
          case kCFStreamEventHasBytesAvailable:
          case kCFStreamEventEndEncountered:
            handle->read_event_.SetReady();
            break;
          case kCFStreamEventErrorOccurred:
            handle->open_event_.SetReady();
            handle->read_event_.SetReady();
            break;
          default:
            GPR_UNREACHABLE_CODE(return );
        }
        CFSTREAM_HANDLE_UNREF(handle, "read callback");
      });
}
void CFStreamHandle::WriteCallback(CFWriteStreamRef stream,
                                   CFStreamEventType type,
                                   void* clientCallBackInfo) {
  CFStreamHandle* handle = static_cast<CFStreamHandle*>(clientCallBackInfo);
  CFSTREAM_HANDLE_REF(handle, "write callback");
  dispatch_async(
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        grpc_core::ExecCtx exec_ctx;
        if (grpc_tcp_trace.enabled()) {
          gpr_log(GPR_DEBUG, "CFStream WriteCallback (%p, %p, %lu, %p)", handle,
                  stream, type, clientCallBackInfo);
        }
        switch (type) {
          case kCFStreamEventOpenCompleted:
            handle->open_event_.SetReady();
            break;
          case kCFStreamEventCanAcceptBytes:
          case kCFStreamEventEndEncountered:
            handle->write_event_.SetReady();
            break;
          case kCFStreamEventErrorOccurred:
            handle->open_event_.SetReady();
            handle->write_event_.SetReady();
            break;
          default:
            GPR_UNREACHABLE_CODE(return );
        }
        CFSTREAM_HANDLE_UNREF(handle, "write callback");
      });
}

CFStreamHandle::CFStreamHandle(CFReadStreamRef read_stream,
                               CFWriteStreamRef write_stream) {
  gpr_ref_init(&refcount_, 1);
  open_event_.InitEvent();
  read_event_.InitEvent();
  write_event_.InitEvent();
  CFStreamClientContext ctx = {0, static_cast<void*>(this),
                               CFStreamHandle::Retain, CFStreamHandle::Release,
                               nil};
  CFReadStreamSetClient(
      read_stream,
      kCFStreamEventOpenCompleted | kCFStreamEventHasBytesAvailable |
          kCFStreamEventErrorOccurred | kCFStreamEventEndEncountered,
      CFStreamHandle::ReadCallback, &ctx);
  CFWriteStreamSetClient(
      write_stream,
      kCFStreamEventOpenCompleted | kCFStreamEventCanAcceptBytes |
          kCFStreamEventErrorOccurred | kCFStreamEventEndEncountered,
      CFStreamHandle::WriteCallback, &ctx);
  CFReadStreamScheduleWithRunLoop(read_stream, CFRunLoopGetMain(),
                                  kCFRunLoopCommonModes);
  CFWriteStreamScheduleWithRunLoop(write_stream, CFRunLoopGetMain(),
                                   kCFRunLoopCommonModes);
}

CFStreamHandle::~CFStreamHandle() {
  open_event_.DestroyEvent();
  read_event_.DestroyEvent();
  write_event_.DestroyEvent();
}

void CFStreamHandle::NotifyOnOpen(grpc_closure* closure) {
  open_event_.NotifyOn(closure);
}

void CFStreamHandle::NotifyOnRead(grpc_closure* closure) {
  read_event_.NotifyOn(closure);
}

void CFStreamHandle::NotifyOnWrite(grpc_closure* closure) {
  write_event_.NotifyOn(closure);
}

void CFStreamHandle::Shutdown(grpc_error* error) {
  open_event_.SetShutdown(GRPC_ERROR_REF(error));
  read_event_.SetShutdown(GRPC_ERROR_REF(error));
  write_event_.SetShutdown(GRPC_ERROR_REF(error));
  GRPC_ERROR_UNREF(error);
}

void CFStreamHandle::Ref(const char* file, int line, const char* reason) {
  if (grpc_tcp_trace.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&refcount_.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "CFStream Handle ref %p : %s %" PRIdPTR " -> %" PRIdPTR, this,
            reason, val, val + 1);
  }
  gpr_ref(&refcount_);
}

void CFStreamHandle::Unref(const char* file, int line, const char* reason) {
  if (grpc_tcp_trace.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&refcount_.count);
    gpr_log(GPR_ERROR,
            "CFStream Handle unref %p : %s %" PRIdPTR " -> %" PRIdPTR, this,
            reason, val, val - 1);
  }
  if (gpr_unref(&refcount_)) {
    delete this;
  }
}

#endif
