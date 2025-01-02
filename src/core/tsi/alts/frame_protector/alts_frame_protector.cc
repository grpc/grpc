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

#include "src/core/tsi/alts/frame_protector/alts_frame_protector.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <memory>

#include "absl/log/log.h"
#include "absl/types/span.h"
#include "src/core/tsi/alts/crypt/gsec.h"
#include "src/core/tsi/alts/frame_protector/alts_crypter.h"
#include "src/core/tsi/alts/frame_protector/frame_handler.h"
#include "src/core/tsi/transport_security.h"
#include "src/core/util/memory.h"

constexpr size_t kMinFrameLength = 1024;
constexpr size_t kDefaultFrameLength = 16 * 1024;
constexpr size_t kMaxFrameLength = 1024 * 1024;

// Limit k on number of frames such that at most 2^(8 * k) frames can be sent.
constexpr size_t kAltsRecordProtocolRekeyFrameLimit = 8;
constexpr size_t kAltsRecordProtocolFrameLimit = 5;

// Main struct for alts_frame_protector.
struct alts_frame_protector {
  tsi_frame_protector base;
  alts_crypter* seal_crypter;
  alts_crypter* unseal_crypter;
  alts_frame_writer* writer;
  alts_frame_reader* reader;
  unsigned char* in_place_protect_buffer;
  unsigned char* in_place_unprotect_buffer;
  size_t in_place_protect_bytes_buffered;
  size_t in_place_unprotect_bytes_processed;
  size_t max_protected_frame_size;
  size_t max_unprotected_frame_size;
  size_t overhead_length;
  size_t counter_overflow;
};

static tsi_result seal(alts_frame_protector* impl) {
  char* error_details = nullptr;
  size_t output_size = 0;
  grpc_status_code status = alts_crypter_process_in_place(
      impl->seal_crypter, impl->in_place_protect_buffer,
      impl->max_protected_frame_size, impl->in_place_protect_bytes_buffered,
      &output_size, &error_details);
  impl->in_place_protect_bytes_buffered = output_size;
  if (status != GRPC_STATUS_OK) {
    LOG(ERROR) << error_details;
    gpr_free(error_details);
    return TSI_INTERNAL_ERROR;
  }
  return TSI_OK;
}

static size_t max_encrypted_payload_bytes(alts_frame_protector* impl) {
  return impl->max_protected_frame_size - kFrameHeaderSize;
}

static tsi_result alts_protect_flush(tsi_frame_protector* self,
                                     unsigned char* protected_output_frames,
                                     size_t* protected_output_frames_size,
                                     size_t* still_pending_size) {
  if (self == nullptr || protected_output_frames == nullptr ||
      protected_output_frames_size == nullptr ||
      still_pending_size == nullptr) {
    LOG(ERROR) << "Invalid nullptr arguments to alts_protect_flush().";
    return TSI_INVALID_ARGUMENT;
  }
  alts_frame_protector* impl = reinterpret_cast<alts_frame_protector*>(self);
  ///
  /// If there's nothing to flush (i.e., in_place_protect_buffer is empty),
  /// we're done.
  ///
  if (impl->in_place_protect_bytes_buffered == 0) {
    *protected_output_frames_size = 0;
    *still_pending_size = 0;
    return TSI_OK;
  }
  ///
  /// If a new frame can start being processed, we encrypt the payload and reset
  /// the frame writer to point to in_place_protect_buffer that holds the newly
  /// sealed frame.
  ///
  if (alts_is_frame_writer_done(impl->writer)) {
    tsi_result result = seal(impl);
    if (result != TSI_OK) {
      return result;
    }
    if (!alts_reset_frame_writer(impl->writer, impl->in_place_protect_buffer,
                                 impl->in_place_protect_bytes_buffered)) {
      LOG(ERROR) << "Couldn't reset frame writer.";
      return TSI_INTERNAL_ERROR;
    }
  }
  ///
  /// Write the sealed frame as much as possible to protected_output_frames.
  /// It's possible a frame will not be written out completely by a single flush
  ///(i.e., still_pending_size != 0), in which case the flush should be called
  /// iteratively until a complete frame has been written out.
  ///
  size_t written_frame_bytes = *protected_output_frames_size;
  if (!alts_write_frame_bytes(impl->writer, protected_output_frames,
                              &written_frame_bytes)) {
    LOG(ERROR) << "Couldn't write frame bytes.";
    return TSI_INTERNAL_ERROR;
  }
  *protected_output_frames_size = written_frame_bytes;
  *still_pending_size = alts_get_num_writer_bytes_remaining(impl->writer);
  ///
  /// If the current frame has been finished processing (i.e., sealed and
  /// written out completely), we empty in_place_protect_buffer.
  ///
  if (alts_is_frame_writer_done(impl->writer)) {
    impl->in_place_protect_bytes_buffered = 0;
  }
  return TSI_OK;
}

static tsi_result alts_protect(tsi_frame_protector* self,
                               const unsigned char* unprotected_bytes,
                               size_t* unprotected_bytes_size,
                               unsigned char* protected_output_frames,
                               size_t* protected_output_frames_size) {
  if (self == nullptr || unprotected_bytes == nullptr ||
      unprotected_bytes_size == nullptr || protected_output_frames == nullptr ||
      protected_output_frames_size == nullptr) {
    LOG(ERROR) << "Invalid nullptr arguments to alts_protect().";
    return TSI_INVALID_ARGUMENT;
  }
  alts_frame_protector* impl = reinterpret_cast<alts_frame_protector*>(self);

  ///
  /// If more payload can be buffered, we buffer it as much as possible to
  /// in_place_protect_buffer.
  ///
  if (impl->in_place_protect_bytes_buffered + impl->overhead_length <
      max_encrypted_payload_bytes(impl)) {
    size_t bytes_to_buffer = std::min(
        *unprotected_bytes_size, max_encrypted_payload_bytes(impl) -
                                     impl->in_place_protect_bytes_buffered -
                                     impl->overhead_length);
    *unprotected_bytes_size = bytes_to_buffer;
    if (bytes_to_buffer > 0) {
      memcpy(
          impl->in_place_protect_buffer + impl->in_place_protect_bytes_buffered,
          unprotected_bytes, bytes_to_buffer);
      impl->in_place_protect_bytes_buffered += bytes_to_buffer;
    }
  } else {
    *unprotected_bytes_size = 0;
  }
  ///
  /// If a full frame has been buffered, we output it. If the first condition
  /// holds, then there exists an unencrypted full frame. If the second
  /// condition holds, then there exists a full frame that has already been
  /// encrypted.
  ///
  if (max_encrypted_payload_bytes(impl) ==
          impl->in_place_protect_bytes_buffered + impl->overhead_length ||
      max_encrypted_payload_bytes(impl) ==
          impl->in_place_protect_bytes_buffered) {
    size_t still_pending_size = 0;
    return alts_protect_flush(self, protected_output_frames,
                              protected_output_frames_size,
                              &still_pending_size);
  } else {
    *protected_output_frames_size = 0;
    return TSI_OK;
  }
}

static tsi_result unseal(alts_frame_protector* impl) {
  char* error_details = nullptr;
  size_t output_size = 0;
  grpc_status_code status = alts_crypter_process_in_place(
      impl->unseal_crypter, impl->in_place_unprotect_buffer,
      impl->max_unprotected_frame_size,
      alts_get_output_bytes_read(impl->reader), &output_size, &error_details);
  if (status != GRPC_STATUS_OK) {
    LOG(ERROR) << error_details;
    gpr_free(error_details);
    return TSI_DATA_CORRUPTED;
  }
  return TSI_OK;
}

static void ensure_buffer_size(alts_frame_protector* impl) {
  if (!alts_has_read_frame_length(impl->reader)) {
    return;
  }
  size_t buffer_space_remaining = impl->max_unprotected_frame_size -
                                  alts_get_output_bytes_read(impl->reader);
  ///
  /// Check if we need to resize in_place_unprotect_buffer in order to hold
  /// remaining bytes of a full frame.
  ///
  if (buffer_space_remaining < alts_get_reader_bytes_remaining(impl->reader)) {
    size_t buffer_len = alts_get_output_bytes_read(impl->reader) +
                        alts_get_reader_bytes_remaining(impl->reader);
    unsigned char* buffer = static_cast<unsigned char*>(gpr_malloc(buffer_len));
    memcpy(buffer, impl->in_place_unprotect_buffer,
           alts_get_output_bytes_read(impl->reader));
    impl->max_unprotected_frame_size = buffer_len;
    gpr_free(impl->in_place_unprotect_buffer);
    impl->in_place_unprotect_buffer = buffer;
    alts_reset_reader_output_buffer(
        impl->reader, buffer + alts_get_output_bytes_read(impl->reader));
  }
}

static tsi_result alts_unprotect(tsi_frame_protector* self,
                                 const unsigned char* protected_frames_bytes,
                                 size_t* protected_frames_bytes_size,
                                 unsigned char* unprotected_bytes,
                                 size_t* unprotected_bytes_size) {
  if (self == nullptr || protected_frames_bytes == nullptr ||
      protected_frames_bytes_size == nullptr || unprotected_bytes == nullptr ||
      unprotected_bytes_size == nullptr) {
    LOG(ERROR) << "Invalid nullptr arguments to alts_unprotect().";
    return TSI_INVALID_ARGUMENT;
  }
  alts_frame_protector* impl = reinterpret_cast<alts_frame_protector*>(self);
  ///
  /// If a new frame can start being processed, we reset the frame reader to
  /// point to in_place_unprotect_buffer that will be used to hold deframed
  /// result.
  ///
  if (alts_is_frame_reader_done(impl->reader) &&
      ((alts_get_output_buffer(impl->reader) == nullptr) ||
       (alts_get_output_bytes_read(impl->reader) ==
        impl->in_place_unprotect_bytes_processed + impl->overhead_length))) {
    if (!alts_reset_frame_reader(impl->reader,
                                 impl->in_place_unprotect_buffer)) {
      LOG(ERROR) << "Couldn't reset frame reader.";
      return TSI_INTERNAL_ERROR;
    }
    impl->in_place_unprotect_bytes_processed = 0;
  }
  ///
  /// If a full frame has not yet been read, we read more bytes from
  /// protected_frames_bytes until a full frame has been read. We also need to
  /// make sure in_place_unprotect_buffer is large enough to hold a complete
  /// frame.
  ///
  if (!alts_is_frame_reader_done(impl->reader)) {
    ensure_buffer_size(impl);
    *protected_frames_bytes_size =
        std::min(impl->max_unprotected_frame_size -
                     alts_get_output_bytes_read(impl->reader),
                 *protected_frames_bytes_size);
    size_t read_frames_bytes_size = *protected_frames_bytes_size;
    if (!alts_read_frame_bytes(impl->reader, protected_frames_bytes,
                               &read_frames_bytes_size)) {
      LOG(ERROR) << "Failed to process frame.";
      return TSI_INTERNAL_ERROR;
    }
    *protected_frames_bytes_size = read_frames_bytes_size;
  } else {
    *protected_frames_bytes_size = 0;
  }
  ///
  /// If a full frame has been read, we unseal it, and write out the
  /// deframed result to unprotected_bytes.
  ///
  if (alts_is_frame_reader_done(impl->reader)) {
    if (impl->in_place_unprotect_bytes_processed == 0) {
      tsi_result result = unseal(impl);
      if (result != TSI_OK) {
        return result;
      }
    }
    size_t bytes_to_write = std::min(
        *unprotected_bytes_size, alts_get_output_bytes_read(impl->reader) -
                                     impl->in_place_unprotect_bytes_processed -
                                     impl->overhead_length);
    if (bytes_to_write > 0) {
      memcpy(unprotected_bytes,
             impl->in_place_unprotect_buffer +
                 impl->in_place_unprotect_bytes_processed,
             bytes_to_write);
    }
    *unprotected_bytes_size = bytes_to_write;
    impl->in_place_unprotect_bytes_processed += bytes_to_write;
    return TSI_OK;
  } else {
    *unprotected_bytes_size = 0;
    return TSI_OK;
  }
}

static void alts_destroy(tsi_frame_protector* self) {
  alts_frame_protector* impl = reinterpret_cast<alts_frame_protector*>(self);
  if (impl != nullptr) {
    alts_crypter_destroy(impl->seal_crypter);
    alts_crypter_destroy(impl->unseal_crypter);
    gpr_free(impl->in_place_protect_buffer);
    gpr_free(impl->in_place_unprotect_buffer);
    alts_destroy_frame_writer(impl->writer);
    alts_destroy_frame_reader(impl->reader);
    gpr_free(impl);
  }
}

static const tsi_frame_protector_vtable alts_frame_protector_vtable = {
    alts_protect, alts_protect_flush, alts_unprotect, alts_destroy};

static grpc_status_code create_alts_crypters(const uint8_t* key,
                                             size_t key_size, bool is_client,
                                             bool is_rekey,
                                             alts_frame_protector* impl,
                                             char** error_details) {
  grpc_status_code status;
  gsec_aead_crypter* aead_crypter_seal = nullptr;
  gsec_aead_crypter* aead_crypter_unseal = nullptr;
  status = gsec_aes_gcm_aead_crypter_create(
      std::make_unique<grpc_core::GsecKey>(absl::MakeConstSpan(key, key_size),
                                           is_rekey),
      kAesGcmNonceLength, kAesGcmTagLength, &aead_crypter_seal, error_details);
  if (status != GRPC_STATUS_OK) {
    return status;
  }
  status = gsec_aes_gcm_aead_crypter_create(
      std::make_unique<grpc_core::GsecKey>(absl::MakeConstSpan(key, key_size),
                                           is_rekey),
      kAesGcmNonceLength, kAesGcmTagLength, &aead_crypter_unseal,
      error_details);
  if (status != GRPC_STATUS_OK) {
    return status;
  }
  size_t overflow_size = is_rekey ? kAltsRecordProtocolRekeyFrameLimit
                                  : kAltsRecordProtocolFrameLimit;
  status = alts_seal_crypter_create(aead_crypter_seal, is_client, overflow_size,
                                    &impl->seal_crypter, error_details);
  if (status != GRPC_STATUS_OK) {
    return status;
  }
  status =
      alts_unseal_crypter_create(aead_crypter_unseal, is_client, overflow_size,
                                 &impl->unseal_crypter, error_details);
  return status;
}

tsi_result alts_create_frame_protector(const uint8_t* key, size_t key_size,
                                       bool is_client, bool is_rekey,
                                       size_t* max_protected_frame_size,
                                       tsi_frame_protector** self) {
  if (key == nullptr || self == nullptr) {
    LOG(ERROR) << "Invalid nullptr arguments to alts_create_frame_protector().";
    return TSI_INTERNAL_ERROR;
  }
  char* error_details = nullptr;
  alts_frame_protector* impl = grpc_core::Zalloc<alts_frame_protector>();
  grpc_status_code status = create_alts_crypters(
      key, key_size, is_client, is_rekey, impl, &error_details);
  if (status != GRPC_STATUS_OK) {
    LOG(ERROR) << "Failed to create ALTS crypters, " << error_details;
    gpr_free(error_details);
    gpr_free(impl);
    return TSI_INTERNAL_ERROR;
  }
  ///
  /// Set maximum frame size to be used by a frame protector. If it is nullptr,
  /// a default frame size will be used. Otherwise, the provided frame size will
  /// be adjusted (if not falling into a valid frame range) and used.
  ///
  size_t max_protected_frame_size_to_set = kDefaultFrameLength;
  if (max_protected_frame_size != nullptr) {
    *max_protected_frame_size =
        std::min(*max_protected_frame_size, kMaxFrameLength);
    *max_protected_frame_size =
        std::max(*max_protected_frame_size, kMinFrameLength);
    max_protected_frame_size_to_set = *max_protected_frame_size;
  }
  impl->max_protected_frame_size = max_protected_frame_size_to_set;
  impl->max_unprotected_frame_size = max_protected_frame_size_to_set;
  impl->in_place_protect_bytes_buffered = 0;
  impl->in_place_unprotect_bytes_processed = 0;
  impl->in_place_protect_buffer = static_cast<unsigned char*>(
      gpr_malloc(sizeof(unsigned char) * max_protected_frame_size_to_set));
  impl->in_place_unprotect_buffer = static_cast<unsigned char*>(
      gpr_malloc(sizeof(unsigned char) * max_protected_frame_size_to_set));
  impl->overhead_length = alts_crypter_num_overhead_bytes(impl->seal_crypter);
  impl->writer = alts_create_frame_writer();
  impl->reader = alts_create_frame_reader();
  impl->base.vtable = &alts_frame_protector_vtable;
  *self = &impl->base;
  return TSI_OK;
}
