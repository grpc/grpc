//
//
// Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.h"

// Privacy-integrity alts_grpc_record_protocol object uses the same struct
// defined in alts_grpc_record_protocol_common.h.

// --- alts_grpc_record_protocol methods implementation. ---

static tsi_result alts_grpc_privacy_integrity_protect(
    alts_grpc_record_protocol* rp, grpc_slice_buffer* unprotected_slices,
    grpc_slice_buffer* protected_slices) {
  // Input sanity check.
  if (rp == nullptr || unprotected_slices == nullptr ||
      protected_slices == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to alts_grpc_record_protocol protect.");
    return TSI_INVALID_ARGUMENT;
  }
  // Allocates memory for output frame. In privacy-integrity protect, the
  // protected frame is stored in a newly allocated buffer.
  size_t protected_frame_size =
      unprotected_slices->length + rp->header_length +
      alts_iovec_record_protocol_get_tag_length(rp->iovec_rp);
  grpc_slice protected_slice = GRPC_SLICE_MALLOC(protected_frame_size);
  iovec_t protected_iovec = {GRPC_SLICE_START_PTR(protected_slice),
                             GRPC_SLICE_LENGTH(protected_slice)};
  // Calls alts_iovec_record_protocol protect.
  char* error_details = nullptr;
  alts_grpc_record_protocol_convert_slice_buffer_to_iovec(rp,
                                                          unprotected_slices);
  grpc_status_code status =
      alts_iovec_record_protocol_privacy_integrity_protect(
          rp->iovec_rp, rp->iovec_buf, unprotected_slices->count,
          protected_iovec, &error_details);
  if (status != GRPC_STATUS_OK) {
    gpr_log(GPR_ERROR, "Failed to protect, %s", error_details);
    gpr_free(error_details);
    grpc_core::CSliceUnref(protected_slice);
    return TSI_INTERNAL_ERROR;
  }
  grpc_slice_buffer_add(protected_slices, protected_slice);
  grpc_slice_buffer_reset_and_unref(unprotected_slices);
  return TSI_OK;
}

static tsi_result alts_grpc_privacy_integrity_unprotect(
    alts_grpc_record_protocol* rp, grpc_slice_buffer* protected_slices,
    grpc_slice_buffer* unprotected_slices) {
  // Input sanity check.
  if (rp == nullptr || protected_slices == nullptr ||
      unprotected_slices == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid nullptr arguments to alts_grpc_record_protocol unprotect.");
    return TSI_INVALID_ARGUMENT;
  }
  // Allocates memory for output frame. In privacy-integrity unprotect, the
  // unprotected data are stored in a newly allocated buffer.
  if (protected_slices->length < rp->header_length + rp->tag_length) {
    gpr_log(GPR_ERROR, "Protected slices do not have sufficient data.");
    return TSI_INVALID_ARGUMENT;
  }
  size_t unprotected_frame_size =
      protected_slices->length - rp->header_length - rp->tag_length;
  grpc_slice unprotected_slice = GRPC_SLICE_MALLOC(unprotected_frame_size);
  iovec_t unprotected_iovec = {GRPC_SLICE_START_PTR(unprotected_slice),
                               GRPC_SLICE_LENGTH(unprotected_slice)};
  // Strips frame header from protected slices.
  grpc_slice_buffer_reset_and_unref(&rp->header_sb);
  grpc_slice_buffer_move_first(protected_slices, rp->header_length,
                               &rp->header_sb);
  iovec_t header_iovec = alts_grpc_record_protocol_get_header_iovec(rp);
  // Calls alts_iovec_record_protocol unprotect.
  char* error_details = nullptr;
  alts_grpc_record_protocol_convert_slice_buffer_to_iovec(rp, protected_slices);
  grpc_status_code status =
      alts_iovec_record_protocol_privacy_integrity_unprotect(
          rp->iovec_rp, header_iovec, rp->iovec_buf, protected_slices->count,
          unprotected_iovec, &error_details);
  if (status != GRPC_STATUS_OK) {
    gpr_log(GPR_ERROR, "Failed to unprotect, %s", error_details);
    gpr_free(error_details);
    grpc_core::CSliceUnref(unprotected_slice);
    return TSI_INTERNAL_ERROR;
  }
  grpc_slice_buffer_reset_and_unref(&rp->header_sb);
  grpc_slice_buffer_reset_and_unref(protected_slices);
  grpc_slice_buffer_add(unprotected_slices, unprotected_slice);
  return TSI_OK;
}

static const alts_grpc_record_protocol_vtable
    alts_grpc_privacy_integrity_record_protocol_vtable = {
        alts_grpc_privacy_integrity_protect,
        alts_grpc_privacy_integrity_unprotect, nullptr};

tsi_result alts_grpc_privacy_integrity_record_protocol_create(
    gsec_aead_crypter* crypter, size_t overflow_size, bool is_client,
    bool is_protect, alts_grpc_record_protocol** rp) {
  if (crypter == nullptr || rp == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to alts_grpc_record_protocol create.");
    return TSI_INVALID_ARGUMENT;
  }
  auto* impl = static_cast<alts_grpc_record_protocol*>(
      gpr_zalloc(sizeof(alts_grpc_record_protocol)));
  // Calls alts_grpc_record_protocol init.
  tsi_result result =
      alts_grpc_record_protocol_init(impl, crypter, overflow_size, is_client,
                                     /*is_integrity_only=*/false, is_protect);
  if (result != TSI_OK) {
    gpr_free(impl);
    return result;
  }
  impl->vtable = &alts_grpc_privacy_integrity_record_protocol_vtable;
  *rp = impl;
  return TSI_OK;
}
