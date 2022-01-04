/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/lib/transport/metadata_batch.h"

#include <stdbool.h>
#include <string.h>

#include "absl/container/inlined_vector.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

void grpc_metadata_batch_set_value(grpc_linked_mdelem* storage,
                                   const grpc_slice& value) {
  grpc_mdelem old_mdelem = storage->md;
  grpc_mdelem new_mdelem = grpc_mdelem_from_slices(
      grpc_slice_ref_internal(GRPC_MDKEY(old_mdelem)), value);
  storage->md = new_mdelem;
  GRPC_MDELEM_UNREF(old_mdelem);
}

namespace {

class CopySink {
 public:
  explicit CopySink(grpc_metadata_batch* dst) : dst_(dst) {}

  void Encode(grpc_mdelem md) {
    // If the mdelem is not external, take a ref.
    // Otherwise, create a new copy, holding its own refs to the
    // underlying slices.
    if (GRPC_MDELEM_STORAGE(md) != GRPC_MDELEM_STORAGE_EXTERNAL) {
      md = GRPC_MDELEM_REF(md);
    } else {
      md = grpc_mdelem_from_slices(grpc_slice_copy(GRPC_MDKEY(md)),
                                   grpc_slice_copy(GRPC_MDVALUE(md)));
    }
    // Error unused in non-debug builds.
    grpc_error_handle GRPC_UNUSED error = dst_->Append(md);
    // The only way that Append() can fail is if
    // there's a duplicate entry for a callout.  However, that can't be
    // the case here, because we would not have been allowed to create
    // a source batch that had that kind of conflict.
    GPR_DEBUG_ASSERT(error == GRPC_ERROR_NONE);
  }

  template <class T, class V>
  void Encode(T trait, V value) {
    dst_->Set(trait, value);
  }

  template <class T>
  void Encode(T trait, const grpc_core::Slice& value) {
    dst_->Set(trait, std::move(value.AsOwned()));
  }

 private:
  grpc_metadata_batch* dst_;
};

}  // namespace

void grpc_metadata_batch_copy(const grpc_metadata_batch* src,
                              grpc_metadata_batch* dst) {
  dst->Clear();
  CopySink sink(dst);
  src->Encode(&sink);
}

grpc_error_handle grpc_attach_md_to_error(grpc_error_handle src,
                                          grpc_mdelem md) {
  grpc_error_handle out = grpc_error_set_str(
      grpc_error_set_str(src, GRPC_ERROR_STR_KEY,
                         grpc_core::StringViewFromSlice(GRPC_MDKEY(md))),
      GRPC_ERROR_STR_VALUE, grpc_core::StringViewFromSlice(GRPC_MDVALUE(md)));
  return out;
}
