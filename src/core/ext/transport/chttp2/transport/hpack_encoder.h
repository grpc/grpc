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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H

#include <grpc/support/port_platform.h>

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder_index.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder_table.h"
#include "src/core/ext/transport/chttp2/transport/popularity_count.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

extern grpc_core::TraceFlag grpc_http_trace;

namespace grpc_core {

// Wrapper to take an array of mdelems and make them encodable
class MetadataArray {
 public:
  MetadataArray(grpc_mdelem** elems, size_t count)
      : elems_(elems), count_(count) {}

  template <typename Encoder>
  void Encode(Encoder* encoder) const {
    for (size_t i = 0; i < count_; i++) {
      encoder->Encode(*elems_[i]);
    }
  }

 private:
  grpc_mdelem** elems_;
  size_t count_;
};

namespace metadata_detail {
template <typename A, typename B>
class ConcatMetadata {
 public:
  ConcatMetadata(const A& a, const B& b) : a_(a), b_(b) {}

  template <typename Encoder>
  void Encode(Encoder* encoder) const {
    a_.Encode(encoder);
    b_.Encode(encoder);
  }

 private:
  const A& a_;
  const B& b_;
};
}  // namespace metadata_detail

template <typename A, typename B>
metadata_detail::ConcatMetadata<A, B> ConcatMetadata(const A& a, const B& b) {
  return metadata_detail::ConcatMetadata<A, B>(a, b);
}

class HPackCompressor {
 public:
  HPackCompressor() = default;
  ~HPackCompressor() = default;

  // Maximum table size we'll actually use.
  static constexpr uint32_t kMaxTableSize = 1024 * 1024;

  void SetMaxTableSize(uint32_t max_table_size);
  void SetMaxUsableSize(uint32_t max_table_size);

  uint32_t test_only_table_size() const {
    return table_.test_only_table_size();
  }

  struct EncodeHeaderOptions {
    uint32_t stream_id;
    bool is_end_of_stream;
    bool use_true_binary_metadata;
    size_t max_frame_size;
    grpc_transport_one_way_stats* stats;
  };

  template <typename HeaderSet>
  void EncodeHeaders(const EncodeHeaderOptions& options,
                     const HeaderSet& headers, grpc_slice_buffer* output) {
    Framer framer(options, this, output);
    headers.Encode(&framer);
  }

  class Framer {
   public:
    Framer(const EncodeHeaderOptions& options, HPackCompressor* compressor,
           grpc_slice_buffer* output);
    ~Framer() { FinishFrame(true); }

    Framer(const Framer&) = delete;
    Framer& operator=(const Framer&) = delete;

    void Encode(grpc_mdelem md);
    void EncodeDeadline(grpc_millis deadline);

   private:
    struct FramePrefix {
      // index (in output_) of the header for the frame
      size_t header_idx;
      // number of bytes in 'output' when we started the frame - used to
      // calculate frame length
      size_t output_length_at_start_of_frame;
    };

    FramePrefix BeginFrame();
    void FinishFrame(bool is_header_boundary);
    void EnsureSpace(size_t need_bytes);

    void AdvertiseTableSizeChange();
    void EmitIndexed(uint32_t index);
    void EncodeDynamic(grpc_mdelem elem);
    static GPR_ATTRIBUTE_NOINLINE void Log(grpc_mdelem elem);

    void EmitLitHdrIncIdx(uint32_t key_index, grpc_mdelem elem);
    void EmitLitHdrNotIdx(uint32_t key_index, grpc_mdelem elem);
    void EmitLitHdrWithStringKeyIncIdx(grpc_mdelem elem);
    void EmitLitHdrWithStringKeyNotIdx(grpc_mdelem elem);

    size_t CurrentFrameSize() const;
    void Add(grpc_slice slice);
    uint8_t* AddTiny(size_t len);

    // maximum size of a frame
    const size_t max_frame_size_;
    bool is_first_frame_ = true;
    const bool use_true_binary_metadata_;
    const bool is_end_of_stream_;
    // output stream id
    const uint32_t stream_id_;
#ifndef NDEBUG
    // have we seen a regular (non-colon-prefixed) header yet?
    bool seen_regular_header_ = false;
#endif
    grpc_slice_buffer* const output_;
    grpc_transport_one_way_stats* const stats_;
    HPackCompressor* const compressor_;
    FramePrefix prefix_;
  };

 private:
  static constexpr size_t kNumFilterValues = 64;

  void AddKeyWithIndex(grpc_slice_refcount* key_ref, uint32_t new_index,
                       uint32_t key_hash);
  void AddElemWithIndex(grpc_mdelem elem, uint32_t new_index,
                        uint32_t elem_hash, uint32_t key_hash);
  void AddElem(grpc_mdelem elem, size_t elem_size, uint32_t elem_hash,
               uint32_t key_hash);
  void AddKey(grpc_mdelem elem, size_t elem_size, uint32_t key_hash);

  // maximum number of bytes we'll use for the decode table (to guard against
  // peers ooming us by setting decode table size high)
  uint32_t max_usable_size_ = hpack_constants::kInitialTableSize;
  // if non-zero, advertise to the decoder that we'll start using a table
  // of this size
  bool advertise_table_size_change_ = false;
  HPackEncoderTable table_;

  // filter tables for elems: this tables provides an approximate
  // popularity count for particular hashes, and are used to determine whether
  // a new literal should be added to the compression table or not.
  // They track a single integer that counts how often a particular value has
  // been seen. When that count reaches max (255), all values are halved.
  grpc_core::PopularityCount<kNumFilterValues> filter_elems_;

  class KeyElem {
   public:
    class Stored {
     public:
      Stored() : elem_(GRPC_MDNULL) {}
      explicit Stored(grpc_mdelem elem) : elem_(GRPC_MDELEM_REF(elem)) {}
      Stored(const Stored& other) : elem_(GRPC_MDELEM_REF(other.elem_)) {}
      Stored& operator=(Stored other) {
        std::swap(elem_, other.elem_);
        return *this;
      }
      ~Stored() { GRPC_MDELEM_UNREF(elem_); }

      const grpc_mdelem& elem() const { return elem_; }

      bool operator==(const Stored& other) const noexcept {
        return elem_.payload == other.elem_.payload;
      }

     private:
      grpc_mdelem elem_;
    };

    KeyElem(grpc_mdelem elem, uint32_t hash) : elem_(elem), hash_(hash) {}
    KeyElem(const KeyElem&);
    KeyElem& operator=(const KeyElem&);

    uint32_t hash() const {
      // TODO(ctiller): unify this with what's in the cc file when we move this
      // code to c++
      return hash_ >> 6;
    }

    Stored stored() const { return Stored(elem_); }

    bool operator==(const Stored& stored) const noexcept {
      return elem_.payload == stored.elem().payload;
    }

   private:
    grpc_mdelem elem_;
    uint32_t hash_;
  };

  class KeySliceRef {
   public:
    using Stored = grpc_core::RefCountedPtr<grpc_slice_refcount>;

    KeySliceRef(grpc_slice_refcount* ref, uint32_t hash)
        : ref_(ref), hash_(hash) {}
    KeySliceRef(const KeySliceRef&) = delete;
    KeySliceRef& operator=(const KeySliceRef&) = delete;

    uint32_t hash() const {
      // TODO(ctiller): unify this with what's in the cc file when we move this
      // code to c++
      return hash_ >> 6;
    }

    Stored stored() const {
      ref_->Ref();
      return Stored(ref_);
    }

    bool operator==(const Stored& stored) const noexcept {
      return ref_ == stored.get();
    }

   private:
    grpc_slice_refcount* ref_;
    uint32_t hash_;
  };

  // entry tables for keys & elems: these tables track values that have been
  // seen and *may* be in the decompressor table
  grpc_core::HPackEncoderIndex<KeyElem, kNumFilterValues> elem_index_;
  grpc_core::HPackEncoderIndex<KeySliceRef, kNumFilterValues> key_index_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_ENCODER_H */
