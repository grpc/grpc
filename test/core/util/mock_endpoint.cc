//
//
// Copyright 2016 gRPC authors.
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
//
//

#include "test/core/util/mock_endpoint.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/sockaddr.h"

typedef struct mock_endpoint {
  grpc_endpoint base;
  gpr_mu mu;
  void (*on_write)(grpc_slice slice);
  grpc_slice_buffer read_buffer;
  grpc_slice_buffer* on_read_out;
  grpc_closure* on_read;
  bool put_reads_done;
  bool destroyed;
} mock_endpoint;

static void me_read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                    grpc_closure* cb, bool /*urgent*/,
                    int /*min_progress_size*/) {
  mock_endpoint* m = reinterpret_cast<mock_endpoint*>(ep);
  gpr_mu_lock(&m->mu);
  if (m->read_buffer.count > 0) {
    grpc_slice_buffer_swap(&m->read_buffer, slices);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, absl::OkStatus());
  } else if (m->put_reads_done) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb,
                            absl::UnavailableError("reads done"));
  } else {
    m->on_read = cb;
    m->on_read_out = slices;
  }
  gpr_mu_unlock(&m->mu);
}

static void me_write(grpc_endpoint* ep, grpc_slice_buffer* slices,
                     grpc_closure* cb, void* /*arg*/, int /*max_frame_size*/) {
  mock_endpoint* m = reinterpret_cast<mock_endpoint*>(ep);
  for (size_t i = 0; i < slices->count; i++) {
    m->on_write(slices->slices[i]);
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, absl::OkStatus());
}

static void me_add_to_pollset(grpc_endpoint* /*ep*/,
                              grpc_pollset* /*pollset*/) {}

static void me_add_to_pollset_set(grpc_endpoint* /*ep*/,
                                  grpc_pollset_set* /*pollset*/) {}

static void me_delete_from_pollset_set(grpc_endpoint* /*ep*/,
                                       grpc_pollset_set* /*pollset*/) {}

static void me_shutdown(grpc_endpoint* ep, grpc_error_handle why) {
  mock_endpoint* m = reinterpret_cast<mock_endpoint*>(ep);
  gpr_mu_lock(&m->mu);
  if (m->on_read) {
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION, m->on_read,
        GRPC_ERROR_CREATE_REFERENCING("Endpoint Shutdown", &why, 1));
    m->on_read = nullptr;
  }
  gpr_mu_unlock(&m->mu);
}

static void destroy(mock_endpoint* m) {
  grpc_slice_buffer_destroy(&m->read_buffer);
  gpr_mu_destroy(&m->mu);
  gpr_free(m);
}

static void me_destroy(grpc_endpoint* ep) {
  mock_endpoint* m = reinterpret_cast<mock_endpoint*>(ep);
  m->destroyed = true;
  if (m->put_reads_done) {
    destroy(m);
  }
}

void grpc_mock_endpoint_finish_put_reads(grpc_endpoint* ep) {
  mock_endpoint* m = reinterpret_cast<mock_endpoint*>(ep);
  m->put_reads_done = true;
  if (m->destroyed) {
    destroy(m);
  }
}

static absl::string_view me_get_peer(grpc_endpoint* /*ep*/) {
  return "fake:mock_endpoint";
}

static absl::string_view me_get_local_address(grpc_endpoint* /*ep*/) {
  return "fake:mock_endpoint";
}

static int me_get_fd(grpc_endpoint* /*ep*/) { return -1; }

static bool me_can_track_err(grpc_endpoint* /*ep*/) { return false; }

static const grpc_endpoint_vtable vtable = {me_read,
                                            me_write,
                                            me_add_to_pollset,
                                            me_add_to_pollset_set,
                                            me_delete_from_pollset_set,
                                            me_shutdown,
                                            me_destroy,
                                            me_get_peer,
                                            me_get_local_address,
                                            me_get_fd,
                                            me_can_track_err};

grpc_endpoint* grpc_mock_endpoint_create(void (*on_write)(grpc_slice slice)) {
  mock_endpoint* m = static_cast<mock_endpoint*>(gpr_malloc(sizeof(*m)));
  m->base.vtable = &vtable;
  grpc_slice_buffer_init(&m->read_buffer);
  gpr_mu_init(&m->mu);
  m->on_write = on_write;
  m->on_read = nullptr;
  m->put_reads_done = false;
  m->destroyed = false;
  return &m->base;
}

void grpc_mock_endpoint_put_read(grpc_endpoint* ep, grpc_slice slice) {
  mock_endpoint* m = reinterpret_cast<mock_endpoint*>(ep);
  gpr_mu_lock(&m->mu);
  GPR_ASSERT(!m->put_reads_done);
  if (m->on_read != nullptr) {
    grpc_slice_buffer_add(m->on_read_out, slice);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, m->on_read, absl::OkStatus());
    m->on_read = nullptr;
  } else {
    grpc_slice_buffer_add(&m->read_buffer, slice);
  }
  gpr_mu_unlock(&m->mu);
}
