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

#ifndef GRPC_SRC_CORE_TSI_ALTS_ZERO_COPY_FRAME_PROTECTOR_ALTS_GRPC_RECORD_PROTOCOL_COMMON_H
#define GRPC_SRC_CORE_TSI_ALTS_ZERO_COPY_FRAME_PROTECTOR_ALTS_GRPC_RECORD_PROTOCOL_COMMON_H

///
/// this file contains alts_grpc_record_protocol internals and internal-only
/// helper functions. The public functions of alts_grpc_record_protocol are
/// defined in the alts_grpc_record_protocol.h.
///

#include <grpc/support/port_platform.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.h"
#include "src/core/tsi/transport_security_grpc.h"
#include "absl/types/span.h"

// V-table for alts_grpc_record_protocol implementations.
struct alts_grpc_record_protocol_vtable {
  tsi_result (*protect)(alts_grpc_record_protocol* self,
                        grpc_slice_buffer* unprotected_slices,
                        grpc_slice_buffer* protected_slices);
  tsi_result (*unprotect)(alts_grpc_record_protocol* self,
                          grpc_slice_buffer* protected_slices,
                          grpc_slice_buffer* unprotected_slices);
  // Destroys and deallocates the concrete object. Required: each concrete
  // record protocol is allocated with its own type and size, so it is the only
  // one that can release it correctly.
  void (*destruct)(alts_grpc_record_protocol* self);
};
// Main struct for alts_grpc_record_protocol implementation, shared by both
// integrity-only record protocol and privacy-integrity record protocol.
// Integrity-only record protocol has additional data elements.
// Privacy-integrity record protocol uses this struct directly.
//
// Instances are created with `new` and released by the concrete type's
// vtable->destruct, because the members below have non-trivial destructors.
struct alts_grpc_record_protocol {
  const alts_grpc_record_protocol_vtable* vtable = nullptr;
  alts_iovec_record_protocol* iovec_rp = nullptr;
  grpc_slice_buffer header_sb;
  // Flat storage for a frame header that arrives split across several slices.
  // Its size is header_length once initialized.
  std::vector<unsigned char> header_buf;
  size_t header_length = 0;
  size_t tag_length = 0;
  // Reusable descriptor storage. Entries borrow slice bytes and are only valid
  // until the current synchronous crypto call returns.
  std::vector<iovec_t> iovec_buf;
  tsi_zero_copy_grpc_protector_allocator_cb alloc_cb = nullptr;
  void* alloc_user_data = nullptr;
};

///
/// Converts the slices of input sb into iovec_t's and stores the result in
/// rp->iovec_buf, reusing its capacity. Returns a view of exactly the
/// converted descriptors, to be passed (data and size together) to the crypto
/// layer. Note that the actual data are not copied, only pointers and lengths
/// are copied. The view is invalidated by the next call that modifies
/// rp->iovec_buf.
///
absl::Span<const iovec_t>
alts_grpc_record_protocol_convert_slice_buffer_to_iovec(
    alts_grpc_record_protocol* rp, const grpc_slice_buffer* sb);

///
/// Stores a single descriptor in rp->iovec_buf and returns a view of it. Used
/// by paths that do not convert a slice buffer.
///
absl::Span<const iovec_t> alts_grpc_record_protocol_set_single_iovec(
    alts_grpc_record_protocol* rp, iovec_t iovec);

///
/// Copies the bytes of a slice buffer into dst. The total length of src must
/// be exactly dst.size(). Returns false, leaving dst unspecified, otherwise.
/// This method is used for copying frame header and tag in case they are
/// stored in multiple slices.
///
bool alts_grpc_record_protocol_copy_slice_buffer(const grpc_slice_buffer* src,
                                                 absl::Span<unsigned char> dst);

///
/// This method returns an iovec object pointing to the frame header stored in
/// rp->header_sb. If the frame header is stored in multiple slices,
/// this method will copy the bytes in rp->header_sb to
/// rp->header_buf, and return an iovec object pointing to
/// rp->header_buf.
///
iovec_t alts_grpc_record_protocol_get_header_iovec(
    alts_grpc_record_protocol* rp);

///
/// Initializes an alts_grpc_record_protocol object, given a gsec_aead_crypter
/// instance, the overflow size of the counter in bytes, a flag indicating if
/// the object is used for client or server side, a flag indicating if it is
/// used for integrity-only or privacy-integrity mode, and a flag indicating if
/// it is for protect or unprotect. The ownership of gsec_aead_crypter object is
/// transferred to the alts_grpc_record_protocol object. On failure, rp is left
/// without owned resources and the crypter stays owned by the caller.
///
tsi_result alts_grpc_record_protocol_init(alts_grpc_record_protocol* rp,
                                          gsec_aead_crypter* crypter,
                                          size_t overflow_size, bool is_client,
                                          bool is_integrity_only,
                                          bool is_protect);

///
/// Releases the resources acquired by alts_grpc_record_protocol_init. It does
/// not deallocate rp itself; that is the job of the concrete type's destruct.
///
void alts_grpc_record_protocol_deinit(alts_grpc_record_protocol* rp);

#endif  // GRPC_SRC_CORE_TSI_ALTS_ZERO_COPY_FRAME_PROTECTOR_ALTS_GRPC_RECORD_PROTOCOL_COMMON_H
