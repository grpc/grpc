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

#include "src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/alts/crypt/gsec.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.h"
#include "src/core/tsi/transport_security_grpc.h"

constexpr size_t kMinFrameLength = 1024;
constexpr size_t kDefaultFrameLength = 16 * 1024;
constexpr size_t kMaxFrameLength = 16 * 1024 * 1024;

/**
 * Main struct for alts_zero_copy_grpc_protector.
 * We choose to have two alts_grpc_record_protocol objects and two sets of slice
 * buffers: one for protect and the other for unprotect, so that protect and
 * unprotect can be executed in parallel. Implementations of this object must be
 * thread compatible.
 */
typedef struct alts_zero_copy_grpc_protector {
  tsi_zero_copy_grpc_protector base;
  alts_grpc_record_protocol* record_protocol;
  alts_grpc_record_protocol* unrecord_protocol;
  size_t max_protected_frame_size;
  size_t max_unprotected_data_size;
  grpc_slice_buffer unprotected_staging_sb;
  grpc_slice_buffer protected_sb;
  grpc_slice_buffer protected_staging_sb;
  uint32_t parsed_frame_size;
} alts_zero_copy_grpc_protector;

/**
 * Given a slice buffer, parses the first 4 bytes little-endian unsigned frame
 * size and returns the total frame size including the frame field. Caller
 * needs to make sure the input slice buffer has at least 4 bytes. Returns true
 * on success and false on failure.
 */
static bool read_frame_size(const grpc_slice_buffer* sb,
                            uint32_t* total_frame_size) {
  if (sb == nullptr || sb->length < kZeroCopyFrameLengthFieldSize) {
    return false;
  }
  uint8_t frame_size_buffer[kZeroCopyFrameLengthFieldSize];
  uint8_t* buf = frame_size_buffer;
  /* Copies the first 4 bytes to a temporary buffer.  */
  size_t remaining = kZeroCopyFrameLengthFieldSize;
  for (size_t i = 0; i < sb->count; i++) {
    size_t slice_length = GRPC_SLICE_LENGTH(sb->slices[i]);
    if (remaining <= slice_length) {
      memcpy(buf, GRPC_SLICE_START_PTR(sb->slices[i]), remaining);
      remaining = 0;
      break;
    } else {
      memcpy(buf, GRPC_SLICE_START_PTR(sb->slices[i]), slice_length);
      buf += slice_length;
      remaining -= slice_length;
    }
  }
  GPR_ASSERT(remaining == 0);
  /* Gets little-endian frame size.  */
  uint32_t frame_size = (static_cast<uint32_t>(frame_size_buffer[3]) << 24) |
                        (static_cast<uint32_t>(frame_size_buffer[2]) << 16) |
                        (static_cast<uint32_t>(frame_size_buffer[1]) << 8) |
                        static_cast<uint32_t>(frame_size_buffer[0]);
  if (frame_size > kMaxFrameLength) {
    gpr_log(GPR_ERROR, "Frame size is larger than maximum frame size");
    return false;
  }
  /* Returns frame size including frame length field.  */
  *total_frame_size =
      static_cast<uint32_t>(frame_size + kZeroCopyFrameLengthFieldSize);
  return true;
}

/**
 * Creates an alts_grpc_record_protocol object, given key, key size, and flags
 * to indicate whether the record_protocol object uses the rekeying AEAD,
 * whether the object is for client or server, whether the object is for
 * integrity-only or privacy-integrity mode, and whether the object is used
 * for protect or unprotect.
 */
static tsi_result create_alts_grpc_record_protocol(
    const uint8_t* key, size_t key_size, bool is_rekey, bool is_client,
    bool is_integrity_only, bool is_protect, bool enable_extra_copy,
    alts_grpc_record_protocol** record_protocol) {
  if (key == nullptr || record_protocol == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  grpc_status_code status;
  gsec_aead_crypter* crypter = nullptr;
  char* error_details = nullptr;
  status = gsec_aes_gcm_aead_crypter_create(key, key_size, kAesGcmNonceLength,
                                            kAesGcmTagLength, is_rekey,
                                            &crypter, &error_details);
  if (status != GRPC_STATUS_OK) {
    gpr_log(GPR_ERROR, "Failed to create AEAD crypter, %s", error_details);
    gpr_free(error_details);
    return TSI_INTERNAL_ERROR;
  }
  size_t overflow_limit = is_rekey ? kAltsRecordProtocolRekeyFrameLimit
                                   : kAltsRecordProtocolFrameLimit;
  /* Creates alts_grpc_record_protocol with AEAD crypter ownership transferred.
   */
  tsi_result result = is_integrity_only
                          ? alts_grpc_integrity_only_record_protocol_create(
                                crypter, overflow_limit, is_client, is_protect,
                                enable_extra_copy, record_protocol)
                          : alts_grpc_privacy_integrity_record_protocol_create(
                                crypter, overflow_limit, is_client, is_protect,
                                record_protocol);
  if (result != TSI_OK) {
    gsec_aead_crypter_destroy(crypter);
    return result;
  }
  return TSI_OK;
}

/* --- tsi_zero_copy_grpc_protector methods implementation. --- */

static tsi_result alts_zero_copy_grpc_protector_protect(
    tsi_zero_copy_grpc_protector* self, grpc_slice_buffer* unprotected_slices,
    grpc_slice_buffer* protected_slices) {
  if (self == nullptr || unprotected_slices == nullptr ||
      protected_slices == nullptr) {
    gpr_log(GPR_ERROR, "Invalid nullptr arguments to zero-copy grpc protect.");
    return TSI_INVALID_ARGUMENT;
  }
  alts_zero_copy_grpc_protector* protector =
      reinterpret_cast<alts_zero_copy_grpc_protector*>(self);
  /* Calls alts_grpc_record_protocol protect repeatly.  */
  while (unprotected_slices->length > protector->max_unprotected_data_size) {
    grpc_slice_buffer_move_first(unprotected_slices,
                                 protector->max_unprotected_data_size,
                                 &protector->unprotected_staging_sb);
    tsi_result status = alts_grpc_record_protocol_protect(
        protector->record_protocol, &protector->unprotected_staging_sb,
        protected_slices);
    if (status != TSI_OK) {
      return status;
    }
  }
  return alts_grpc_record_protocol_protect(
      protector->record_protocol, unprotected_slices, protected_slices);
}

static tsi_result alts_zero_copy_grpc_protector_unprotect(
    tsi_zero_copy_grpc_protector* self, grpc_slice_buffer* protected_slices,
    grpc_slice_buffer* unprotected_slices, int* min_progress_size) {
  if (self == nullptr || unprotected_slices == nullptr ||
      protected_slices == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid nullptr arguments to zero-copy grpc unprotect.");
    return TSI_INVALID_ARGUMENT;
  }
  alts_zero_copy_grpc_protector* protector =
      reinterpret_cast<alts_zero_copy_grpc_protector*>(self);
  grpc_slice_buffer_move_into(protected_slices, &protector->protected_sb);
  /* Keep unprotecting each frame if possible.  */
  while (protector->protected_sb.length >= kZeroCopyFrameLengthFieldSize) {
    if (protector->parsed_frame_size == 0) {
      /* We have not parsed frame size yet. Parses frame size.  */
      if (!read_frame_size(&protector->protected_sb,
                           &protector->parsed_frame_size)) {
        grpc_slice_buffer_reset_and_unref(&protector->protected_sb);
        return TSI_DATA_CORRUPTED;
      }
    }
    if (protector->protected_sb.length < protector->parsed_frame_size) break;
    /* At this point, protected_sb contains at least one frame of data.  */
    tsi_result status;
    if (protector->protected_sb.length == protector->parsed_frame_size) {
      status = alts_grpc_record_protocol_unprotect(protector->unrecord_protocol,
                                                   &protector->protected_sb,
                                                   unprotected_slices);
    } else {
      grpc_slice_buffer_move_first(&protector->protected_sb,
                                   protector->parsed_frame_size,
                                   &protector->protected_staging_sb);
      status = alts_grpc_record_protocol_unprotect(
          protector->unrecord_protocol, &protector->protected_staging_sb,
          unprotected_slices);
    }
    protector->parsed_frame_size = 0;
    if (status != TSI_OK) {
      grpc_slice_buffer_reset_and_unref(&protector->protected_sb);
      return status;
    }
  }
  if (min_progress_size != nullptr) {
    if (protector->parsed_frame_size > kZeroCopyFrameLengthFieldSize) {
      *min_progress_size =
          protector->parsed_frame_size - protector->protected_sb.length;
    } else {
      *min_progress_size = 1;
    }
  }
  return TSI_OK;
}

static void alts_zero_copy_grpc_protector_destroy(
    tsi_zero_copy_grpc_protector* self) {
  if (self == nullptr) {
    return;
  }
  alts_zero_copy_grpc_protector* protector =
      reinterpret_cast<alts_zero_copy_grpc_protector*>(self);
  alts_grpc_record_protocol_destroy(protector->record_protocol);
  alts_grpc_record_protocol_destroy(protector->unrecord_protocol);
  grpc_slice_buffer_destroy(&protector->unprotected_staging_sb);
  grpc_slice_buffer_destroy(&protector->protected_sb);
  grpc_slice_buffer_destroy(&protector->protected_staging_sb);
  gpr_free(protector);
}

static tsi_result alts_zero_copy_grpc_protector_max_frame_size(
    tsi_zero_copy_grpc_protector* self, size_t* max_frame_size) {
  if (self == nullptr || max_frame_size == nullptr) return TSI_INVALID_ARGUMENT;
  alts_zero_copy_grpc_protector* protector =
      reinterpret_cast<alts_zero_copy_grpc_protector*>(self);
  *max_frame_size = protector->max_protected_frame_size;
  return TSI_OK;
}

static const tsi_zero_copy_grpc_protector_vtable
    alts_zero_copy_grpc_protector_vtable = {
        alts_zero_copy_grpc_protector_protect,
        alts_zero_copy_grpc_protector_unprotect,
        alts_zero_copy_grpc_protector_destroy,
        alts_zero_copy_grpc_protector_max_frame_size};

tsi_result alts_zero_copy_grpc_protector_create(
    const uint8_t* key, size_t key_size, bool is_rekey, bool is_client,
    bool is_integrity_only, bool enable_extra_copy,
    size_t* max_protected_frame_size,
    tsi_zero_copy_grpc_protector** protector) {
  if (grpc_core::ExecCtx::Get() == nullptr || key == nullptr ||
      protector == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Invalid nullptr arguments to alts_zero_copy_grpc_protector create.");
    return TSI_INVALID_ARGUMENT;
  }
  /* Creates alts_zero_copy_protector.  */
  alts_zero_copy_grpc_protector* impl =
      static_cast<alts_zero_copy_grpc_protector*>(
          gpr_zalloc(sizeof(alts_zero_copy_grpc_protector)));
  /* Creates alts_grpc_record_protocol objects.  */
  tsi_result status = create_alts_grpc_record_protocol(
      key, key_size, is_rekey, is_client, is_integrity_only,
      /*is_protect=*/true, enable_extra_copy, &impl->record_protocol);
  if (status == TSI_OK) {
    status = create_alts_grpc_record_protocol(
        key, key_size, is_rekey, is_client, is_integrity_only,
        /*is_protect=*/false, enable_extra_copy, &impl->unrecord_protocol);
    if (status == TSI_OK) {
      /* Sets maximum frame size.  */
      size_t max_protected_frame_size_to_set = kDefaultFrameLength;
      if (max_protected_frame_size != nullptr) {
        *max_protected_frame_size =
            std::min(*max_protected_frame_size, kMaxFrameLength);
        *max_protected_frame_size =
            std::max(*max_protected_frame_size, kMinFrameLength);
        max_protected_frame_size_to_set = *max_protected_frame_size;
      }
      impl->max_protected_frame_size = max_protected_frame_size_to_set;
      impl->max_unprotected_data_size =
          alts_grpc_record_protocol_max_unprotected_data_size(
              impl->record_protocol, max_protected_frame_size_to_set);
      GPR_ASSERT(impl->max_unprotected_data_size > 0);
      /* Allocates internal slice buffers.  */
      grpc_slice_buffer_init(&impl->unprotected_staging_sb);
      grpc_slice_buffer_init(&impl->protected_sb);
      grpc_slice_buffer_init(&impl->protected_staging_sb);
      impl->parsed_frame_size = 0;
      impl->base.vtable = &alts_zero_copy_grpc_protector_vtable;
      *protector = &impl->base;
      return TSI_OK;
    }
  }

  /* Cleanup if create failed.  */
  alts_grpc_record_protocol_destroy(impl->record_protocol);
  alts_grpc_record_protocol_destroy(impl->unrecord_protocol);
  gpr_free(impl);
  return TSI_INTERNAL_ERROR;
}
