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

#ifdef GRPC_CFSTREAM_TCP

#import <CoreFoundation/CoreFoundation.h>
#import "src/core/lib/iomgr/tcp_cfstream.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error_apple.h"
#include "src/core/lib/iomgr/tcp_cfstream_sync.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

typedef struct {
  grpc_endpoint base;
  gpr_refcount refcount;

  CFReadStreamRef read_stream;
  CFWriteStreamRef write_stream;
  CFStreamSync* stream_sync;

  grpc_closure* read_cb;
  grpc_closure* write_cb;
  grpc_slice_buffer* read_slices;
  grpc_slice_buffer* write_slices;

  grpc_closure read_action;
  grpc_closure write_action;
  CFStreamEventType read_type;

  char* peer_string;
  grpc_resource_user* resource_user;
  grpc_resource_user_slice_allocator slice_allocator;
} CFStreamTCP;

static void TCPFree(CFStreamTCP* tcp) {
  grpc_resource_user_unref(tcp->resource_user);
  CFRelease(tcp->read_stream);
  CFRelease(tcp->write_stream);
  CFSTREAM_SYNC_UNREF(tcp->stream_sync, "free");
  gpr_free(tcp->peer_string);
  gpr_free(tcp);
}

#ifndef NDEBUG
#define TCP_REF(tcp, reason) tcp_ref((tcp), (reason), __FILE__, __LINE__)
#define TCP_UNREF(tcp, reason) tcp_unref((tcp), (reason), __FILE__, __LINE__)
static void tcp_unref(CFStreamTCP* tcp, const char* reason, const char* file,
                      int line) {
  if (grpc_tcp_trace.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&tcp->refcount.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "TCP unref %p : %s %" PRIdPTR " -> %" PRIdPTR, tcp, reason, val,
            val - 1);
  }
  if (gpr_unref(&tcp->refcount)) {
    TCPFree(tcp);
  }
}
static void tcp_ref(CFStreamTCP* tcp, const char* reason, const char* file,
                    int line) {
  if (grpc_tcp_trace.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&tcp->refcount.count);
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "TCP   ref %p : %s %" PRIdPTR " -> %" PRIdPTR, tcp, reason, val,
            val + 1);
  }
  gpr_ref(&tcp->refcount);
}
#else
#define TCP_REF(tcp, reason) tcp_ref((tcp))
#define TCP_UNREF(tcp, reason) tcp_unref((tcp))
static void tcp_unref(CFStreamTCP* tcp) {
  if (gpr_unref(&tcp->refcount)) {
    TCPFree(tcp);
  }
}
static void tcp_ref(CFStreamTCP* tcp) { gpr_ref(&tcp->refcount); }
#endif

static grpc_error* TCPAnnotateError(grpc_error* src_error, CFStreamTCP* tcp) {
  return grpc_error_set_str(
      grpc_error_set_int(src_error, GRPC_ERROR_INT_GRPC_STATUS,
                         GRPC_STATUS_UNAVAILABLE),
      GRPC_ERROR_STR_TARGET_ADDRESS,
      grpc_slice_from_copied_string(tcp->peer_string));
}

static void CallReadCB(CFStreamTCP* tcp, grpc_error* error) {
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "TCP:%p call_read_cb %p %p:%p", tcp, tcp->read_cb,
            tcp->read_cb->cb, tcp->read_cb->cb_arg);
    size_t i;
    const char* str = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "read: error=%s", str);

    for (i = 0; i < tcp->read_slices->count; i++) {
      char* dump = grpc_dump_slice(tcp->read_slices->slices[i],
                                   GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "READ %p (peer=%s): %s", tcp, tcp->peer_string, dump);
      gpr_free(dump);
    }
  }
  grpc_closure* cb = tcp->read_cb;
  tcp->read_cb = nullptr;
  tcp->read_slices = nullptr;
  GRPC_CLOSURE_SCHED(cb, error);
}

static void CallWriteCB(CFStreamTCP* tcp, grpc_error* error) {
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "TCP:%p call_write_cb %p %p:%p", tcp, tcp->write_cb,
            tcp->write_cb->cb, tcp->write_cb->cb_arg);
    const char* str = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "write: error=%s", str);
  }
  grpc_closure* cb = tcp->write_cb;
  tcp->write_cb = nullptr;
  tcp->write_slices = nullptr;
  GRPC_CLOSURE_SCHED(cb, error);
}

static void ReadAction(void* arg, grpc_error* error) {
  CFStreamTCP* tcp = static_cast<CFStreamTCP*>(arg);
  GPR_ASSERT(tcp->read_cb != nullptr);
  if (error) {
    grpc_slice_buffer_reset_and_unref_internal(tcp->read_slices);
    CallReadCB(tcp, GRPC_ERROR_REF(error));
    TCP_UNREF(tcp, "read");
    return;
  }

  GPR_ASSERT(tcp->read_slices->count == 1);
  grpc_slice slice = tcp->read_slices->slices[0];
  size_t len = GRPC_SLICE_LENGTH(slice);
  CFIndex read_size =
      CFReadStreamRead(tcp->read_stream, GRPC_SLICE_START_PTR(slice), len);
  if (read_size == -1) {
    grpc_slice_buffer_reset_and_unref_internal(tcp->read_slices);
    CFErrorRef stream_error = CFReadStreamCopyError(tcp->read_stream);
    if (stream_error != nullptr) {
      error = TCPAnnotateError(GRPC_ERROR_CREATE_FROM_CFERROR(
                                   stream_error, "Read error"),
                               tcp);
      CFRelease(stream_error);
    } else {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Read error");
    }
    CallReadCB(tcp, error);
    TCP_UNREF(tcp, "read");
  } else if (read_size == 0) {
    grpc_slice_buffer_reset_and_unref_internal(tcp->read_slices);
    CallReadCB(tcp,
               TCPAnnotateError(
                   GRPC_ERROR_CREATE_FROM_STATIC_STRING("Socket closed"), tcp));
    TCP_UNREF(tcp, "read");
  } else {
    if (read_size < len) {
      grpc_slice_buffer_trim_end(tcp->read_slices, len - read_size, nullptr);
    }
    CallReadCB(tcp, GRPC_ERROR_NONE);
    TCP_UNREF(tcp, "read");
  }
}

static void WriteAction(void* arg, grpc_error* error) {
  CFStreamTCP* tcp = static_cast<CFStreamTCP*>(arg);
  GPR_ASSERT(tcp->write_cb != nullptr);
  if (error) {
    grpc_slice_buffer_reset_and_unref_internal(tcp->write_slices);
    CallWriteCB(tcp, GRPC_ERROR_REF(error));
    TCP_UNREF(tcp, "write");
    return;
  }

  grpc_slice slice = grpc_slice_buffer_take_first(tcp->write_slices);
  size_t slice_len = GRPC_SLICE_LENGTH(slice);
  CFIndex write_size = CFWriteStreamWrite(
      tcp->write_stream, GRPC_SLICE_START_PTR(slice), slice_len);
  if (write_size == -1) {
    grpc_slice_buffer_reset_and_unref_internal(tcp->write_slices);
    CFErrorRef stream_error = CFWriteStreamCopyError(tcp->write_stream);
    if (stream_error != nullptr) {
      error = TCPAnnotateError(GRPC_ERROR_CREATE_FROM_CFERROR(
                                   stream_error, "write failed."),
                               tcp);
      CFRelease(stream_error);
    } else {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("write failed.");
    }
    CallWriteCB(tcp, error);
    TCP_UNREF(tcp, "write");
  } else {
    if (write_size < GRPC_SLICE_LENGTH(slice)) {
      grpc_slice_buffer_undo_take_first(
          tcp->write_slices, grpc_slice_sub(slice, write_size, slice_len));
    }
    if (tcp->write_slices->length > 0) {
      tcp->stream_sync->NotifyOnWrite(&tcp->write_action);
    } else {
      CallWriteCB(tcp, GRPC_ERROR_NONE);
      TCP_UNREF(tcp, "write");
    }

    if (grpc_tcp_trace.enabled()) {
      grpc_slice trace_slice = grpc_slice_sub(slice, 0, write_size);
      char* dump = grpc_dump_slice(trace_slice, GPR_DUMP_HEX | GPR_DUMP_ASCII);
      gpr_log(GPR_DEBUG, "WRITE %p (peer=%s): %s", tcp, tcp->peer_string, dump);
      gpr_free(dump);
      grpc_slice_unref_internal(trace_slice);
    }
  }
  grpc_slice_unref_internal(slice);
}

static void TCPReadAllocationDone(void* arg, grpc_error* error) {
  CFStreamTCP* tcp = static_cast<CFStreamTCP*>(arg);
  if (error == GRPC_ERROR_NONE) {
    tcp->stream_sync->NotifyOnRead(&tcp->read_action);
  } else {
    grpc_slice_buffer_reset_and_unref_internal(tcp->read_slices);
    CallReadCB(tcp, error);
    TCP_UNREF(tcp, "read");
  }
}

static void TCPRead(grpc_endpoint* ep, grpc_slice_buffer* slices,
                    grpc_closure* cb) {
  CFStreamTCP* tcp = reinterpret_cast<CFStreamTCP*>(ep);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "tcp:%p read (%p, %p) length:%zu", tcp, slices, cb,
            slices->length);
  }
  GPR_ASSERT(tcp->read_cb == nullptr);
  tcp->read_cb = cb;
  tcp->read_slices = slices;
  grpc_slice_buffer_reset_and_unref_internal(slices);
  grpc_resource_user_alloc_slices(&tcp->slice_allocator,
                                  GRPC_TCP_DEFAULT_READ_SLICE_SIZE, 1,
                                  tcp->read_slices);
  TCP_REF(tcp, "read");
}

static void TCPWrite(grpc_endpoint* ep, grpc_slice_buffer* slices,
                     grpc_closure* cb) {
  CFStreamTCP* tcp = reinterpret_cast<CFStreamTCP*>(ep);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "tcp:%p write (%p, %p) length:%zu", tcp, slices, cb,
            slices->length);
  }
  GPR_ASSERT(tcp->write_cb == nullptr);
  tcp->write_cb = cb;
  tcp->write_slices = slices;
  TCP_REF(tcp, "write");
  tcp->stream_sync->NotifyOnWrite(&tcp->write_action);
}

void TCPShutdown(grpc_endpoint* ep, grpc_error* why) {
  CFStreamTCP* tcp = reinterpret_cast<CFStreamTCP*>(ep);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "tcp:%p shutdown (%p)", tcp, why);
  }
  CFReadStreamClose(tcp->read_stream);
  CFWriteStreamClose(tcp->write_stream);
  tcp->stream_sync->Shutdown(why);
  grpc_resource_user_shutdown(tcp->resource_user);
}

void TCPDestroy(grpc_endpoint* ep) {
  CFStreamTCP* tcp = reinterpret_cast<CFStreamTCP*>(ep);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "tcp:%p destroy", tcp);
  }
  TCP_UNREF(tcp, "destroy");
}

grpc_resource_user* TCPGetResourceUser(grpc_endpoint* ep) {
  CFStreamTCP* tcp = reinterpret_cast<CFStreamTCP*>(ep);
  return tcp->resource_user;
}

char* TCPGetPeer(grpc_endpoint* ep) {
  CFStreamTCP* tcp = reinterpret_cast<CFStreamTCP*>(ep);
  return gpr_strdup(tcp->peer_string);
}

int TCPGetFD(grpc_endpoint* ep) { return 0; }

void TCPAddToPollset(grpc_endpoint* ep, grpc_pollset* pollset) {}
void TCPAddToPollsetSet(grpc_endpoint* ep, grpc_pollset_set* pollset) {}
void TCPDeleteFromPollsetSet(grpc_endpoint* ep, grpc_pollset_set* pollset) {}

static const grpc_endpoint_vtable vtable = {TCPRead,
                                            TCPWrite,
                                            TCPAddToPollset,
                                            TCPAddToPollsetSet,
                                            TCPDeleteFromPollsetSet,
                                            TCPShutdown,
                                            TCPDestroy,
                                            TCPGetResourceUser,
                                            TCPGetPeer,
                                            TCPGetFD};

grpc_endpoint* grpc_tcp_create(CFReadStreamRef read_stream,
                               CFWriteStreamRef write_stream,
                               const char* peer_string,
                               grpc_resource_quota* resource_quota,
                               CFStreamSync* stream_sync) {
  CFStreamTCP* tcp = static_cast<CFStreamTCP*>(gpr_malloc(sizeof(CFStreamTCP)));
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "tcp:%p create readStream:%p writeStream: %p", tcp,
            read_stream, write_stream);
  }
  tcp->base.vtable = &vtable;
  gpr_ref_init(&tcp->refcount, 1);
  tcp->read_stream = read_stream;
  tcp->write_stream = write_stream;
  CFRetain(read_stream);
  CFRetain(write_stream);
  tcp->stream_sync = stream_sync;
  CFSTREAM_SYNC_REF(tcp->stream_sync, "endpoint create");

  tcp->peer_string = gpr_strdup(peer_string);
  tcp->read_cb = nil;
  tcp->write_cb = nil;
  tcp->read_slices = nil;
  tcp->write_slices = nil;
  GRPC_CLOSURE_INIT(&tcp->read_action, ReadAction, static_cast<void*>(tcp),
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&tcp->write_action, WriteAction, static_cast<void*>(tcp),
                    grpc_schedule_on_exec_ctx);
  tcp->resource_user = grpc_resource_user_create(resource_quota, peer_string);
  grpc_resource_user_slice_allocator_init(
      &tcp->slice_allocator, tcp->resource_user, TCPReadAllocationDone, tcp);

  return &tcp->base;
}

#endif /* GRPC_CFSTREAM_TCP */
