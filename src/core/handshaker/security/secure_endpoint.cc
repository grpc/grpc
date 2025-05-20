//
//
// Copyright 2015 gRPC authors.
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

#include "src/core/handshaker/security/secure_endpoint.h"

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/memory_request.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include <inttypes.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <regex>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/tsi/transport_security_grpc.h"
#include "src/core/tsi/transport_security_interface.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"
#include "src/core/util/sync.h"

#define STAGING_BUFFER_SIZE 8192

static void on_read(void* user_data, grpc_error_handle error);
static void on_write(void* user_data, grpc_error_handle error);

namespace grpc_core {
namespace {
class FrameProtector : public RefCounted<FrameProtector> {
 public:
  FrameProtector(tsi_frame_protector* protector,
                 tsi_zero_copy_grpc_protector* zero_copy_protector,
                 grpc_slice* leftover_slices, size_t leftover_nslices,
                 const ChannelArgs& args)
      : protector_(protector),
        zero_copy_protector_(zero_copy_protector),
        memory_owner_(args.GetObject<ResourceQuota>()
                          ->memory_quota()
                          ->CreateMemoryOwner()),
        self_reservation_(memory_owner_.MakeReservation(sizeof(*this))) {
    GRPC_TRACE_LOG(secure_endpoint, INFO)
        << "FrameProtector: " << this << " protector: " << protector_
        << " zero_copy_protector: " << zero_copy_protector_
        << " leftover_nslices: " << leftover_nslices;
    if (leftover_nslices > 0) {
      leftover_bytes_ = std::make_unique<SliceBuffer>();
      for (size_t i = 0; i < leftover_nslices; i++) {
        leftover_bytes_->Append(Slice(CSliceRef(leftover_slices[i])));
      }
    }
    if (zero_copy_protector_ != nullptr) {
      read_staging_buffer_ = grpc_empty_slice();
      write_staging_buffer_ = grpc_empty_slice();
    } else {
      read_staging_buffer_ =
          memory_owner_.MakeSlice(MemoryRequest(STAGING_BUFFER_SIZE));
      write_staging_buffer_ =
          memory_owner_.MakeSlice(MemoryRequest(STAGING_BUFFER_SIZE));
    }
  }

  ~FrameProtector() override {
    tsi_frame_protector_destroy(protector_);
    tsi_zero_copy_grpc_protector_destroy(zero_copy_protector_);
    CSliceUnref(read_staging_buffer_);
    CSliceUnref(write_staging_buffer_);
  }

  Mutex* read_mu() ABSL_LOCK_RETURNED(read_mu_) { return &read_mu_; }
  Mutex* write_mu() ABSL_LOCK_RETURNED(write_mu_) { return &write_mu_; }

  void TraceOp(absl::string_view op, grpc_slice_buffer* slices) {
    if (GRPC_TRACE_FLAG_ENABLED(secure_endpoint)) {
      size_t i;
      if (slices->length < 64) {
        for (i = 0; i < slices->count; i++) {
          char* data =
              grpc_dump_slice(slices->slices[i], GPR_DUMP_HEX | GPR_DUMP_ASCII);
          LOG(INFO) << op << " " << this << ": " << data;
          gpr_free(data);
        }
      } else {
        grpc_slice first = GRPC_SLICE_MALLOC(64);
        grpc_slice_buffer_copy_first_into_buffer(slices, 64,
                                                 GRPC_SLICE_START_PTR(first));
        char* data = grpc_dump_slice(first, GPR_DUMP_HEX | GPR_DUMP_ASCII);
        LOG(INFO) << op << " first:" << this << ": " << data;
        gpr_free(data);
        CSliceUnref(first);
      }
    }
  }

  void MaybePostReclaimer() {
    if (!has_posted_reclaimer_.exchange(true, std::memory_order_relaxed)) {
      memory_owner_.PostReclaimer(
          ReclamationPass::kBenign,
          [self = Ref()](std::optional<ReclamationSweep> sweep) {
            if (sweep.has_value()) {
              GRPC_TRACE_LOG(resource_quota, INFO)
                  << "secure endpoint: benign reclamation to free memory";
              grpc_slice temp_read_slice;
              grpc_slice temp_write_slice;

              self->read_mu_.Lock();
              temp_read_slice =
                  std::exchange(self->read_staging_buffer_, grpc_empty_slice());
              self->read_mu_.Unlock();

              self->write_mu_.Lock();
              temp_write_slice = std::exchange(self->write_staging_buffer_,
                                               grpc_empty_slice());
              self->write_mu_.Unlock();

              CSliceUnref(temp_read_slice);
              CSliceUnref(temp_write_slice);
              self->has_posted_reclaimer_.store(false,
                                                std::memory_order_relaxed);
            }
          });
    }
  }

  void FlushReadStagingBuffer(uint8_t** cur, uint8_t** end)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mu_) {
    grpc_slice_buffer_add_indexed(read_buffer_, read_staging_buffer_);
    read_staging_buffer_ =
        memory_owner_.MakeSlice(MemoryRequest(STAGING_BUFFER_SIZE));
    *cur = GRPC_SLICE_START_PTR(read_staging_buffer_);
    *end = GRPC_SLICE_END_PTR(read_staging_buffer_);
  }

  void FinishRead(bool ok) {
    TraceOp("FinishRead", read_buffer_);
    // TODO(yangg) experiment with moving this block after read_cb to see if it
    // helps latency
    source_buffer_.Clear();
    if (!ok) grpc_slice_buffer_reset_and_unref(read_buffer_);
    read_buffer_ = nullptr;
  }

  absl::Status Unprotect(absl::Status read_status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mu_) {
    GRPC_LATENT_SEE_INNER_SCOPE("unprotect");
    bool keep_looping = false;
    tsi_result result = TSI_OK;

    uint8_t* cur = GRPC_SLICE_START_PTR(read_staging_buffer_);
    uint8_t* end = GRPC_SLICE_END_PTR(read_staging_buffer_);

    if (!read_status.ok()) {
      grpc_slice_buffer_reset_and_unref(read_buffer_);
    } else if (zero_copy_protector_ != nullptr) {
      // Use zero-copy grpc protector to unprotect.
      int min_progress_size = 1;
      // Get the size of the last frame which is not yet fully decrypted.
      // This estimated frame size is stored in ep->min_progress_size which is
      // passed to the TCP layer to indicate the minimum number of
      // bytes that need to be read to make meaningful progress. This would
      // avoid reading of small slices from the network.
      // TODO(vigneshbabu): Set min_progress_size in the regular
      // (non-zero-copy) frame protector code path as well.
      result = tsi_zero_copy_grpc_protector_unprotect(
          zero_copy_protector_, source_buffer_.c_slice_buffer(), read_buffer_,
          &min_progress_size);
      min_progress_size = std::max(1, min_progress_size);
      min_progress_size_ = result != TSI_OK ? 1 : min_progress_size;
    } else {
      // Use frame protector to unprotect.
      // TODO(yangg) check error, maybe bail out early
      for (size_t i = 0; i < source_buffer_.Count(); i++) {
        grpc_slice encrypted = source_buffer_.c_slice_buffer()->slices[i];
        uint8_t* message_bytes = GRPC_SLICE_START_PTR(encrypted);
        size_t message_size = GRPC_SLICE_LENGTH(encrypted);

        while (message_size > 0 || keep_looping) {
          size_t unprotected_buffer_size_written =
              static_cast<size_t>(end - cur);
          size_t processed_message_size = message_size;
          if (IsTsiFrameProtectorWithoutLocksEnabled()) {
            result = tsi_frame_protector_unprotect(
                protector_, message_bytes, &processed_message_size, cur,
                &unprotected_buffer_size_written);
          } else {
            protector_mu_.Lock();
            result = tsi_frame_protector_unprotect(
                protector_, message_bytes, &processed_message_size, cur,
                &unprotected_buffer_size_written);
            protector_mu_.Unlock();
          }
          if (result != TSI_OK) {
            LOG(ERROR) << "Decryption error: " << tsi_result_to_string(result);
            break;
          }
          message_bytes += processed_message_size;
          message_size -= processed_message_size;
          cur += unprotected_buffer_size_written;

          if (cur == end) {
            FlushReadStagingBuffer(&cur, &end);
            // Force to enter the loop again to extract buffered bytes in
            // protector. The bytes could be buffered because of running out
            // of staging_buffer. If this happens at the end of all slices,
            // doing another unprotect avoids leaving data in the protector.
            keep_looping = true;
          } else if (unprotected_buffer_size_written > 0) {
            keep_looping = true;
          } else {
            keep_looping = false;
          }
        }
        if (result != TSI_OK) break;
      }

      if (cur != GRPC_SLICE_START_PTR(read_staging_buffer_)) {
        grpc_slice_buffer_add(
            read_buffer_,
            grpc_slice_split_head(
                &read_staging_buffer_,
                static_cast<size_t>(
                    cur - GRPC_SLICE_START_PTR(read_staging_buffer_))));
      }
    }

    if (read_status.ok() && result != TSI_OK) {
      read_status = GRPC_ERROR_CREATE(
          absl::StrCat("Unwrap failed (", tsi_result_to_string(result), ")"));
    }

    GRPC_TRACE_LOG(secure_endpoint, INFO)
        << "Unprotect: " << this << " read_status: " << read_status;

    return read_status;
  }

  void BeginRead(grpc_slice_buffer* slices) {
    read_buffer_ = slices;
    grpc_slice_buffer_reset_and_unref(read_buffer_);
  }

  bool MaybeCompleteReadImmediately() {
    GRPC_TRACE_LOG(secure_endpoint, INFO)
        << "MaybeCompleteReadImmediately: " << this
        << " leftover_bytes_: " << leftover_bytes_.get();
    if (leftover_bytes_ != nullptr) {
      grpc_slice_buffer_swap(leftover_bytes_->c_slice_buffer(),
                             source_buffer_.c_slice_buffer());
      leftover_bytes_.reset();
      return true;
    }
    return false;
  }

  grpc_event_engine::experimental::SliceBuffer* source_buffer() {
    return &source_buffer_;
  }

  int min_progress_size() const { return min_progress_size_; }

  void FlushWriteStagingBuffer(uint8_t** cur, uint8_t** end)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(write_mu_) {
    output_buffer_.AppendIndexed(
        grpc_event_engine::experimental::Slice(write_staging_buffer_));
    write_staging_buffer_ =
        memory_owner_.MakeSlice(MemoryRequest(STAGING_BUFFER_SIZE));
    *cur = GRPC_SLICE_START_PTR(write_staging_buffer_);
    *end = GRPC_SLICE_END_PTR(write_staging_buffer_);
    MaybePostReclaimer();
  }

  tsi_result Protect(grpc_slice_buffer* slices, int max_frame_size)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(write_mu_) {
    GRPC_LATENT_SEE_INNER_SCOPE("protect");
    uint8_t* cur = GRPC_SLICE_START_PTR(write_staging_buffer_);
    uint8_t* end = GRPC_SLICE_END_PTR(write_staging_buffer_);

    output_buffer_.Clear();

    TraceOp("Protect", slices);

    tsi_result result = TSI_OK;
    if (zero_copy_protector_ != nullptr) {
      // Use zero-copy grpc protector to protect.
      // Break the input slices into chunks of size = max_frame_size and call
      // tsi_zero_copy_grpc_protector_protect on each chunk. This ensures that
      // the protector cannot create frames larger than the specified
      // max_frame_size.
      while (slices->length > static_cast<size_t>(max_frame_size) &&
             result == TSI_OK) {
        grpc_slice_buffer_move_first(
            slices, static_cast<size_t>(max_frame_size),
            protector_staging_buffer_.c_slice_buffer());
        result = tsi_zero_copy_grpc_protector_protect(
            zero_copy_protector_, protector_staging_buffer_.c_slice_buffer(),
            output_buffer_.c_slice_buffer());
      }
      if (result == TSI_OK && slices->length > 0) {
        result = tsi_zero_copy_grpc_protector_protect(
            zero_copy_protector_, slices, output_buffer_.c_slice_buffer());
      }
      protector_staging_buffer_.Clear();
    } else {
      // Use frame protector to protect.
      for (size_t i = 0; i < slices->count; i++) {
        grpc_slice plain = slices->slices[i];
        uint8_t* message_bytes = GRPC_SLICE_START_PTR(plain);
        size_t message_size = GRPC_SLICE_LENGTH(plain);
        while (message_size > 0) {
          size_t protected_buffer_size_to_send = static_cast<size_t>(end - cur);
          size_t processed_message_size = message_size;
          if (IsTsiFrameProtectorWithoutLocksEnabled()) {
            result = tsi_frame_protector_protect(
                protector_, message_bytes, &processed_message_size, cur,
                &protected_buffer_size_to_send);
          } else {
            protector_mu_.Lock();
            result = tsi_frame_protector_protect(
                protector_, message_bytes, &processed_message_size, cur,
                &protected_buffer_size_to_send);
            protector_mu_.Unlock();
          }
          if (result != TSI_OK) {
            LOG(ERROR) << "Encryption error: " << tsi_result_to_string(result);
            break;
          }
          message_bytes += processed_message_size;
          message_size -= processed_message_size;
          cur += protected_buffer_size_to_send;

          if (cur == end) {
            FlushWriteStagingBuffer(&cur, &end);
          }
        }
        if (result != TSI_OK) break;
      }
      if (result == TSI_OK) {
        size_t still_pending_size;
        do {
          size_t protected_buffer_size_to_send = static_cast<size_t>(end - cur);
          if (IsTsiFrameProtectorWithoutLocksEnabled()) {
            result = tsi_frame_protector_protect_flush(
                protector_, cur, &protected_buffer_size_to_send,
                &still_pending_size);
          } else {
            protector_mu_.Lock();
            result = tsi_frame_protector_protect_flush(
                protector_, cur, &protected_buffer_size_to_send,
                &still_pending_size);
            protector_mu_.Unlock();
          }
          if (result != TSI_OK) break;
          cur += protected_buffer_size_to_send;
          if (cur == end) {
            FlushWriteStagingBuffer(&cur, &end);
          }
        } while (still_pending_size > 0);
        if (cur != GRPC_SLICE_START_PTR(write_staging_buffer_)) {
          output_buffer_.Append(
              grpc_event_engine::experimental::Slice(grpc_slice_split_head(
                  &write_staging_buffer_,
                  static_cast<size_t>(
                      cur - GRPC_SLICE_START_PTR(write_staging_buffer_)))));
        }
      }
    }
    // TODO(yangg) do different things according to the error type?
    if (result != TSI_OK) output_buffer_.Clear();
    return result;
  }

  grpc_event_engine::experimental::SliceBuffer* output_buffer() {
    return &output_buffer_;
  }

  void Shutdown() { memory_owner_.Reset(); }

 private:
  struct tsi_frame_protector* const protector_;
  struct tsi_zero_copy_grpc_protector* const zero_copy_protector_;
  Mutex mu_;
  Mutex read_mu_;
  Mutex write_mu_;
  Mutex protector_mu_;
  grpc_slice_buffer* read_buffer_ = nullptr;
  grpc_event_engine::experimental::SliceBuffer source_buffer_;
  // saved handshaker leftover data to unprotect.
  std::unique_ptr<SliceBuffer> leftover_bytes_;
  // buffers for read and write
  grpc_slice read_staging_buffer_ ABSL_GUARDED_BY(read_mu_);
  grpc_slice write_staging_buffer_ ABSL_GUARDED_BY(write_mu_);
  grpc_event_engine::experimental::SliceBuffer output_buffer_;
  MemoryOwner memory_owner_;
  MemoryAllocator::Reservation self_reservation_;
  std::atomic<bool> has_posted_reclaimer_{false};
  int min_progress_size_ = 1;
  SliceBuffer protector_staging_buffer_;
};
}  // namespace
}  // namespace grpc_core

namespace {
struct secure_endpoint : public grpc_endpoint {
  secure_endpoint(const grpc_endpoint_vtable* vtbl,
                  tsi_frame_protector* protector,
                  tsi_zero_copy_grpc_protector* zero_copy_protector,
                  grpc_core::OrphanablePtr<grpc_endpoint> endpoint,
                  grpc_slice* leftover_slices, size_t leftover_nslices,
                  const grpc_core::ChannelArgs& args)
      : wrapped_ep(std::move(endpoint)),
        frame_protector(protector, zero_copy_protector, leftover_slices,
                        leftover_nslices, args) {
    this->vtable = vtbl;
    GRPC_CLOSURE_INIT(&on_read, ::on_read, this, grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&on_write, ::on_write, this, grpc_schedule_on_exec_ctx);
    gpr_ref_init(&ref, 1);
  }

  ~secure_endpoint() {}

  grpc_core::OrphanablePtr<grpc_endpoint> wrapped_ep;
  grpc_core::FrameProtector frame_protector;
  // saved upper level callbacks and user_data.
  grpc_closure* read_cb = nullptr;
  grpc_closure* write_cb = nullptr;
  grpc_closure on_read;
  grpc_closure on_write;
  gpr_refcount ref;
};
}  // namespace

static void destroy(secure_endpoint* ep) { delete ep; }

#ifndef NDEBUG
#define SECURE_ENDPOINT_UNREF(ep, reason) \
  secure_endpoint_unref((ep), (reason), __FILE__, __LINE__)
#define SECURE_ENDPOINT_REF(ep, reason) \
  secure_endpoint_ref((ep), (reason), __FILE__, __LINE__)
static void secure_endpoint_unref(secure_endpoint* ep, const char* reason,
                                  const char* file, int line) {
  if (GRPC_TRACE_FLAG_ENABLED(secure_endpoint)) {
    gpr_atm val = gpr_atm_no_barrier_load(&ep->ref.count);
    VLOG(2).AtLocation(file, line) << "SECENDP unref " << ep << " : " << reason
                                   << " " << val << " -> " << val - 1;
  }
  if (gpr_unref(&ep->ref)) {
    destroy(ep);
  }
}

static void secure_endpoint_ref(secure_endpoint* ep, const char* reason,
                                const char* file, int line) {
  if (GRPC_TRACE_FLAG_ENABLED(secure_endpoint)) {
    gpr_atm val = gpr_atm_no_barrier_load(&ep->ref.count);
    VLOG(2).AtLocation(file, line) << "SECENDP   ref " << ep << " : " << reason
                                   << " " << val << " -> " << val + 1;
  }
  gpr_ref(&ep->ref);
}
#else
#define SECURE_ENDPOINT_UNREF(ep, reason) secure_endpoint_unref((ep))
#define SECURE_ENDPOINT_REF(ep, reason) secure_endpoint_ref((ep))
static void secure_endpoint_unref(secure_endpoint* ep) {
  if (gpr_unref(&ep->ref)) {
    destroy(ep);
  }
}

static void secure_endpoint_ref(secure_endpoint* ep) { gpr_ref(&ep->ref); }
#endif

static void call_read_cb(secure_endpoint* ep, grpc_error_handle error) {
  ep->frame_protector.FinishRead(error.ok());
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, ep->read_cb, error);
  SECURE_ENDPOINT_UNREF(ep, "read");
}

static void on_read(void* user_data, grpc_error_handle error) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(user_data);

  {
    grpc_core::MutexLock lock(ep->frame_protector.read_mu());
    // If we were shut down after this callback was scheduled with OK
    // status but before it was invoked, we need to treat that as an error.
    if (ep->wrapped_ep == nullptr && error.ok()) {
      error = absl::CancelledError("secure endpoint shutdown");
    }
    error = ep->frame_protector.Unprotect(std::move(error));
  }

  if (!error.ok()) {
    call_read_cb(
        ep, GRPC_ERROR_CREATE_REFERENCING("Secure read failed", &error, 1));
    return;
  }

  call_read_cb(ep, absl::OkStatus());
}

static void endpoint_read(grpc_endpoint* secure_ep, grpc_slice_buffer* slices,
                          grpc_closure* cb, bool urgent,
                          int /*min_progress_size*/) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  ep->read_cb = cb;
  ep->frame_protector.BeginRead(slices);

  SECURE_ENDPOINT_REF(ep, "read");
  if (ep->frame_protector.MaybeCompleteReadImmediately()) {
    on_read(ep, absl::OkStatus());
    return;
  }

  grpc_endpoint_read(ep->wrapped_ep.get(),
                     ep->frame_protector.source_buffer()->c_slice_buffer(),
                     &ep->on_read, urgent,
                     ep->frame_protector.min_progress_size());
}

static void on_write(void* user_data, grpc_error_handle error) {
  secure_endpoint* ep = static_cast<secure_endpoint*>(user_data);
  grpc_closure* cb = ep->write_cb;
  ep->write_cb = nullptr;
  SECURE_ENDPOINT_UNREF(ep, "write");
  grpc_core::EnsureRunInExecCtx([cb, error = std::move(error)]() {
    grpc_core::Closure::Run(DEBUG_LOCATION, cb, error);
  });
}

static void endpoint_write(grpc_endpoint* secure_ep, grpc_slice_buffer* slices,
                           grpc_closure* cb, void* arg, int max_frame_size) {
  GRPC_LATENT_SEE_INNER_SCOPE("secure_endpoint write");
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  tsi_result result;
  {
    grpc_core::MutexLock lock(ep->frame_protector.write_mu());
    result = ep->frame_protector.Protect(slices, max_frame_size);
  }

  if (result != TSI_OK) {
    grpc_core::ExecCtx::Run(
        DEBUG_LOCATION, cb,
        GRPC_ERROR_CREATE(
            absl::StrCat("Wrap failed (", tsi_result_to_string(result), ")")));
    return;
  }

  // Need to hold a ref here, because the wrapped endpoint may access
  // output_buffer at any time until the write completes.
  SECURE_ENDPOINT_REF(ep, "write");
  ep->write_cb = cb;
  grpc_endpoint_write(ep->wrapped_ep.get(),
                      ep->frame_protector.output_buffer()->c_slice_buffer(),
                      &ep->on_write, arg, max_frame_size);
}

static void endpoint_destroy(grpc_endpoint* secure_ep) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  ep->frame_protector.read_mu()->Lock();
  ep->wrapped_ep.reset();
  ep->frame_protector.Shutdown();
  ep->frame_protector.read_mu()->Unlock();
  SECURE_ENDPOINT_UNREF(ep, "destroy");
}

static void endpoint_add_to_pollset(grpc_endpoint* secure_ep,
                                    grpc_pollset* pollset) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  grpc_endpoint_add_to_pollset(ep->wrapped_ep.get(), pollset);
}

static void endpoint_add_to_pollset_set(grpc_endpoint* secure_ep,
                                        grpc_pollset_set* pollset_set) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  grpc_endpoint_add_to_pollset_set(ep->wrapped_ep.get(), pollset_set);
}

static void endpoint_delete_from_pollset_set(grpc_endpoint* secure_ep,
                                             grpc_pollset_set* pollset_set) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  grpc_endpoint_delete_from_pollset_set(ep->wrapped_ep.get(), pollset_set);
}

static absl::string_view endpoint_get_peer(grpc_endpoint* secure_ep) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  return grpc_endpoint_get_peer(ep->wrapped_ep.get());
}

static absl::string_view endpoint_get_local_address(grpc_endpoint* secure_ep) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  return grpc_endpoint_get_local_address(ep->wrapped_ep.get());
}

static int endpoint_get_fd(grpc_endpoint* secure_ep) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  return grpc_endpoint_get_fd(ep->wrapped_ep.get());
}

static bool endpoint_can_track_err(grpc_endpoint* secure_ep) {
  secure_endpoint* ep = reinterpret_cast<secure_endpoint*>(secure_ep);
  return grpc_endpoint_can_track_err(ep->wrapped_ep.get());
}

static const grpc_endpoint_vtable vtable = {endpoint_read,
                                            endpoint_write,
                                            endpoint_add_to_pollset,
                                            endpoint_add_to_pollset_set,
                                            endpoint_delete_from_pollset_set,
                                            endpoint_destroy,
                                            endpoint_get_peer,
                                            endpoint_get_local_address,
                                            endpoint_get_fd,
                                            endpoint_can_track_err};

namespace grpc_event_engine::experimental {
namespace {

class SecureEndpoint final : public EventEngine::Endpoint {
 public:
  SecureEndpoint(
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          wrapped_ep,
      struct tsi_frame_protector* protector,
      struct tsi_zero_copy_grpc_protector* zero_copy_protector,
      grpc_slice* leftover_slices, size_t leftover_nslices,
      const grpc_core::ChannelArgs& channel_args)
      : impl_(grpc_core::MakeRefCounted<Impl>(
            std::move(wrapped_ep), protector, zero_copy_protector,
            leftover_slices, leftover_nslices, channel_args)) {}

  ~SecureEndpoint() override { impl_->Shutdown(); }

  bool Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            ReadArgs in_args) override {
    return impl_->Read(std::move(on_read), buffer, std::move(in_args));
  }

  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, WriteArgs args) override {
    return impl_->Write(std::move(on_writable), data, std::move(args));
  }

  const EventEngine::ResolvedAddress& GetPeerAddress() const override {
    return impl_->GetPeerAddress();
  }

  const EventEngine::ResolvedAddress& GetLocalAddress() const override {
    return impl_->GetLocalAddress();
  }

  void* QueryExtension(absl::string_view id) override {
    return impl_->QueryExtension(id);
  }

  std::vector<size_t> AllWriteMetrics() override {
    return impl_->AllWriteMetrics();
  }

  std::optional<absl::string_view> GetMetricName(size_t key) override {
    return impl_->GetMetricName(key);
  }

  std::optional<size_t> GetMetricKey(absl::string_view name) override {
    return impl_->GetMetricKey(name);
  }

 private:
  class Impl : public grpc_core::RefCounted<Impl> {
   public:
    Impl(std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
             wrapped_ep,
         struct tsi_frame_protector* protector,
         struct tsi_zero_copy_grpc_protector* zero_copy_protector,
         grpc_slice* leftover_slices, size_t leftover_nslices,
         const grpc_core::ChannelArgs& channel_args)
        : frame_protector_(protector, zero_copy_protector, leftover_slices,
                           leftover_nslices, channel_args),
          wrapped_ep_(std::move(wrapped_ep)),
          event_engine_(channel_args.GetObjectRef<
                        grpc_event_engine::experimental::EventEngine>()),
          large_read_threshold_(std::max(
              1, channel_args.GetInt(GRPC_ARG_DECRYPTION_OFFLOAD_THRESHOLD)
                     .value_or(32 * 1024))),
          large_write_threshold_(std::max(
              1, channel_args.GetInt(GRPC_ARG_ENCRYPTION_OFFLOAD_THRESHOLD)
                     .value_or(32 * 1024))),
          max_buffered_writes_(std::max(
              0, channel_args
                     .GetInt(GRPC_ARG_ENCRYPTION_OFFLOAD_MAX_BUFFERED_WRITES)
                     .value_or(1024 * 1024))) {}

    bool Read(absl::AnyInvocable<void(absl::Status)> on_read,
              SliceBuffer* buffer, ReadArgs args) {
      on_read_ = std::move(on_read);
      frame_protector_.BeginRead(buffer->c_slice_buffer());
      if (frame_protector_.MaybeCompleteReadImmediately()) {
        return MaybeFinishReadImmediately();
      }
      args.set_read_hint_bytes(frame_protector_.min_progress_size());
      bool read_completed_immediately = wrapped_ep_->Read(
          [impl = Ref()](absl::Status status) mutable {
            FinishAsyncRead(std::move(impl), std::move(status));
          },
          frame_protector_.source_buffer(), std::move(args));
      if (read_completed_immediately) return MaybeFinishReadImmediately();
      return false;
    }

    bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
               SliceBuffer* data, WriteArgs args) {
      GRPC_LATENT_SEE_INNER_SCOPE("secure_endpoint write");
      tsi_result result;
      frame_protector_.TraceOp("Write", data->c_slice_buffer());
      if (grpc_core::IsSecureEndpointOffloadLargeWritesEnabled()) {
        // If we get a zero length frame, just complete without looking at
        // anything further
        if (data->Length() == 0) return true;
        grpc_core::MutexLock lock(&write_queue_mu_);
        // If there's been a failure observed asynchronously, then fail out with
        // that error.
        if (!writing_.ok()) {
          event_engine_->Run(
              [on_write = std::move(on_writable),
               status = writing_.status()]() mutable { on_write(status); });
          return false;
        }
        // If we're already writing (== encrypting on another thread) we need to
        // queue the writes up to do after that completes.
        // OR if we're not already writing but this write is large, we push it
        // onto the event engine to encrypt later.
        if (*writing_ || data->Length() > large_write_threshold_) {
          // Since we don't call on_write until we've collected pending_writes
          // in the FinishAsyncWrites path, and EventEngine insists that one
          // write finishes before a second begins, we should never see a Write
          // call here with a non-null pending_writes_.
          CHECK(pending_writes_ == nullptr);
          pending_writes_ = std::make_unique<SliceBuffer>(std::move(*data));
          frame_protector_.TraceOp("Pending",
                                   pending_writes_->c_slice_buffer());
          // Wait for the previous write to finish before considering this one.
          // Note that since EventEngine::Endpoint allows only one outstanding
          // write, this will pause sending until the callback is invoked.
          last_write_args_ = std::move(args);
          on_write_ = std::move(on_writable);
          if (!*writing_) {
            writing_ = true;
            event_engine_->Run([impl = Ref()]() mutable {
              FinishAsyncWrite(std::move(impl));
            });
          }
          return false;
        }
      }
      // A small write: encrypt inline and write to the socket.
      {
        grpc_core::MutexLock lock(frame_protector_.write_mu());
        result = frame_protector_.Protect(data->c_slice_buffer(),
                                          args.max_frame_size());
      }
      if (result != TSI_OK) {
        event_engine_->Run(
            [on_writable = std::move(on_writable), result]() mutable {
              on_writable(GRPC_ERROR_CREATE(absl::StrCat(
                  "Wrap failed (", tsi_result_to_string(result), ")")));
            });
        return false;
      }
      on_write_ = std::move(on_writable);
      frame_protector_.TraceOp(
          "Write", frame_protector_.output_buffer()->c_slice_buffer());
      return wrapped_ep_->Write(
          [impl = Ref()](absl::Status status) mutable {
            auto on_write = std::move(impl->on_write_);
            impl.reset();
            on_write(status);
          },
          frame_protector_.output_buffer(), std::move(args));
    }

    const EventEngine::ResolvedAddress& GetPeerAddress() const {
      return wrapped_ep_->GetPeerAddress();
    }

    const EventEngine::ResolvedAddress& GetLocalAddress() const {
      return wrapped_ep_->GetLocalAddress();
    }

    void* QueryExtension(absl::string_view id) {
      return wrapped_ep_->QueryExtension(id);
    }

    void Shutdown() {
      std::unique_ptr<EventEngine::Endpoint> wrapped_ep;
      grpc_core::MutexLock write_lock(frame_protector_.write_mu());
      grpc_core::MutexLock read_lock(frame_protector_.read_mu());
      wrapped_ep = std::move(wrapped_ep_);
      frame_protector_.Shutdown();
    }

    virtual std::vector<size_t> AllWriteMetrics() {
      return wrapped_ep_->AllWriteMetrics();
    }

    virtual std::optional<absl::string_view> GetMetricName(size_t key) {
      return wrapped_ep_->GetMetricName(key);
    }

    virtual std::optional<size_t> GetMetricKey(absl::string_view name) {
      return wrapped_ep_->GetMetricKey(name);
    }

   private:
    bool MaybeFinishReadImmediately() {
      GRPC_LATENT_SEE_INNER_SCOPE("secure_endpoint maybe finish read");
      grpc_core::MutexLock lock(frame_protector_.read_mu());
      // If the read is large, since we got the bytes whilst still calling read,
      // offload the decryption to event engine.
      // That way we can do the decryption off this thread (which is usually
      // under some mutual exclusion device) and publish the bytes using the
      // async callback path.
      // (remember there's at most one outstanding read and write on an
      // EventEngine::Endpoint allowed, so this doesn't risk ordering issues.)
      if (grpc_core::IsSecureEndpointOffloadLargeReadsEnabled() &&
          frame_protector_.source_buffer()->Length() > large_read_threshold_) {
        event_engine_->Run([impl = Ref()]() mutable {
          FinishAsyncRead(std::move(impl), absl::OkStatus());
        });
        return false;
      }
      frame_protector_.TraceOp(
          "Read(Imm)", frame_protector_.source_buffer()->c_slice_buffer());
      auto status = frame_protector_.Unprotect(absl::OkStatus());
      frame_protector_.FinishRead(status.ok());
      if (status.ok()) return true;
      event_engine_->Run([impl = Ref(), status = std::move(status)]() mutable {
        auto on_read = std::move(impl->on_read_);
        impl.reset();
        on_read(status);
      });
      return false;
    }

    static void FinishAsyncRead(grpc_core::RefCountedPtr<Impl> impl,
                                absl::Status status) {
      GRPC_LATENT_SEE_PARENT_SCOPE("secure endpoint finish async read");
      {
        grpc_core::MutexLock lock(impl->frame_protector_.read_mu());
        if (status.ok() && impl->wrapped_ep_ == nullptr) {
          status = absl::CancelledError("secure endpoint shutdown");
        }
        status = impl->frame_protector_.Unprotect(std::move(status));
      }
      if (status.ok()) {
        impl->frame_protector_.TraceOp(
            "Read", impl->frame_protector_.source_buffer()->c_slice_buffer());
      }
      auto on_read = std::move(impl->on_read_);
      impl->frame_protector_.FinishRead(status.ok());
      impl.reset();
      on_read(status);
    }

    std::string WritingString() ABSL_EXCLUSIVE_LOCKS_REQUIRED(write_queue_mu_) {
      if (!writing_.ok()) return writing_.status().ToString();
      return *writing_ ? "true" : "false";
    }

    static void FailWrites(grpc_core::RefCountedPtr<Impl> impl,
                           absl::Status status)
        ABSL_LOCKS_EXCLUDED(frame_protector_.write_mu(), write_queue_mu_) {
      impl->write_queue_mu_.Lock();
      impl->writing_ = status;
      auto on_write = std::move(impl->on_write_);
      impl->write_queue_mu_.Unlock();
      impl.reset();
      if (on_write != nullptr) on_write(status);
    };

    static void FinishAsyncWrite(grpc_core::RefCountedPtr<Impl> impl) {
      GRPC_LATENT_SEE_PARENT_SCOPE("secure endpoint finish async write");
      tsi_result result;
      std::unique_ptr<SliceBuffer> data;
      WriteArgs args;
      // If writes complete immediately we'll loop back to here.
      while (true) {
        {
          // Check to see if we've written all the bytes.
          grpc_core::ReleasableMutexLock lock(&impl->write_queue_mu_);
          if (impl->pending_writes_ == nullptr) {
            impl->writing_ = false;
            DCHECK(impl->on_write_ == nullptr);
            lock.Release();
            return;
          }
          // There's more data - grab it under the queue lock.
          data = std::move(impl->pending_writes_);
          impl->frame_protector_.TraceOp("data", data->c_slice_buffer());
          args = std::move(impl->last_write_args_);
          DCHECK(impl->on_write_ != nullptr);
        }
        impl->event_engine_->Run(
            [on_write = std::move(impl->on_write_)]() mutable {
              on_write(absl::OkStatus());
            });
        // Now grab the frame protector write mutex - this is held for some
        // time (we do the encryption inside of it) - so it's a different
        // mutex to the queue mutex above.
        grpc_core::ReleasableMutexLock lock(impl->frame_protector_.write_mu());
        // If the endpoint closed whilst waiting for this callback, then fail
        // out the write and we're done.
        if (impl->wrapped_ep_ == nullptr) {
          lock.Release();
          FailWrites(std::move(impl),
                     absl::CancelledError("secure endpoint shutdown"));
          return;
        }
        result = impl->frame_protector_.Protect(data->c_slice_buffer(),
                                                args.max_frame_size());
        if (result != TSI_OK) {
          lock.Release();
          // Protection failed... fail the write and we're done.
          FailWrites(std::move(impl),
                     GRPC_ERROR_CREATE(absl::StrCat(
                         "Wrap failed (", tsi_result_to_string(result), ")")));
          return;
        }
        // Write out the protected bytes - returns true if it finishes
        // immediately, in which case we'll loop.
        const bool write_finished_immediately = impl->wrapped_ep_->Write(
            [impl](absl::Status status) mutable {
              // Async completion path: if we completed successfully then loop
              // back into FinishAsyncWrite to see if there's more writing to
              // do.
              if (status.ok()) {
                FinishAsyncWrite(std::move(impl));
                return;
              }
              // Write failed: push the failure up via the callback if it's
              // there.
              FailWrites(std::move(impl), status);
            },
            impl->frame_protector_.output_buffer(), std::move(args));
        if (!write_finished_immediately) break;
      }
    }

    grpc_core::Mutex write_queue_mu_;
    absl::StatusOr<bool> writing_ ABSL_GUARDED_BY(write_queue_mu_) = false;
    WriteArgs last_write_args_ ABSL_GUARDED_BY(write_queue_mu_);
    std::unique_ptr<SliceBuffer> pending_writes_
        ABSL_GUARDED_BY(write_queue_mu_);
    grpc_core::FrameProtector frame_protector_;
    absl::AnyInvocable<void(absl::Status)> on_read_;
    absl::AnyInvocable<void(absl::Status)> on_write_;
    std::unique_ptr<EventEngine::Endpoint> wrapped_ep_;
    std::shared_ptr<EventEngine> event_engine_;
    const size_t large_read_threshold_;
    const size_t large_write_threshold_;
    const size_t max_buffered_writes_;
  };

  grpc_core::RefCountedPtr<Impl> impl_;
};

}  // namespace
}  // namespace grpc_event_engine::experimental

grpc_core::OrphanablePtr<grpc_endpoint> grpc_secure_endpoint_create(
    struct tsi_frame_protector* protector,
    struct tsi_zero_copy_grpc_protector* zero_copy_protector,
    grpc_core::OrphanablePtr<grpc_endpoint> to_wrap,
    grpc_slice* leftover_slices, size_t leftover_nslices,
    const grpc_core::ChannelArgs& channel_args) {
  if (!grpc_core::IsEventEngineSecureEndpointEnabled()) {
    return grpc_legacy_secure_endpoint_create(
        protector, zero_copy_protector, std::move(to_wrap), leftover_slices,
        channel_args.ToC().get(), leftover_nslices);
  }
  if (grpc_event_engine::experimental::grpc_get_wrapped_event_engine_endpoint(
          to_wrap.get()) != nullptr) {
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        event_engine_endpoint = grpc_event_engine::experimental::
            grpc_take_wrapped_event_engine_endpoint(to_wrap.release());
    CHECK(event_engine_endpoint != nullptr);
    return grpc_core::OrphanablePtr<grpc_endpoint>(
        grpc_event_engine::experimental::grpc_event_engine_endpoint_create(
            std::make_unique<grpc_event_engine::experimental::SecureEndpoint>(
                std::move(event_engine_endpoint), protector,
                zero_copy_protector, leftover_slices, leftover_nslices,
                channel_args)));
  }
  return grpc_core::MakeOrphanable<secure_endpoint>(
      &vtable, protector, zero_copy_protector, std::move(to_wrap),
      leftover_slices, leftover_nslices, channel_args);
}
