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

#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"

#include <assert.h>
#include <string.h>

/* This is here for grpc_is_binary_header
 * TODO(murgatroid99): Remove this
 */
#include <grpc/grpc.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_table.h"
#include "src/core/ext/transport/chttp2/transport/varint.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/timeout_encoding.h"

#define HASH_FRAGMENT_1(x) ((x)&255)
#define HASH_FRAGMENT_2(x) ((x >> 8) & 255)
#define HASH_FRAGMENT_3(x) ((x >> 16) & 255)
#define HASH_FRAGMENT_4(x) ((x >> 24) & 255)

/* don't consider adding anything bigger than this to the hpack table */
#define MAX_DECODER_SPACE_USAGE 512

namespace grpc_core {
namespace chttp2 {

namespace {
class WireEncodedValue {
 public:
  WireEncodedValue(grpc_slice slice, bool is_binary_header,
                   bool true_binary_enabled) {
    if (is_binary_header) {
      if (true_binary_enabled) {
        GRPC_STATS_INC_HPACK_SEND_BINARY();
        huffman_prefix_ = 0x00;
        insert_null_before_wire_value_ = true;
        data_ = grpc_slice_ref_internal(slice);

      } else {
        GRPC_STATS_INC_HPACK_SEND_BINARY_BASE64();
        huffman_prefix_ = 0x80;
        insert_null_before_wire_value_ = false;
        data_ = grpc_chttp2_base64_encode_and_huffman_compress(slice);
      }
    } else {
      /* TODO(ctiller): opportunistically compress non-binary headers */
      GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
      huffman_prefix_ = 0x00;
      insert_null_before_wire_value_ = false;
      data_ = grpc_slice_ref_internal(slice);
    }
  }

  size_t Length() const {
    return GPR_SLICE_LENGTH(data_) + insert_null_before_wire_value_;
  }

  uint8_t huffman_prefix() const { return huffman_prefix_; }

  size_t ValuePrefixLength() const { return insert_null_before_wire_value_; }

  void WriteValuePrefix(uint8_t* p) const {
    if (insert_null_before_wire_value_) *p = 0;
  }

  grpc_slice value() { return data_; }

 private:
  uint8_t huffman_prefix_;
  bool insert_null_before_wire_value_;
  grpc_slice data_;
};
}  // namespace

class HpackEncoder::Framer {
 public:
  Framer(HpackEncoder* encoder, uint32_t stream_id, const FrameOptions& options,
         grpc_slice_buffer* output)
      : encoder_(encoder),
        stream_id_(stream_id),
        output_(output),
        stats_(options.stats()),
        max_frame_size_(options.max_frame_size()),
        use_true_binary_metadata_(options.use_true_binary_metadata()),
        is_eof_(options.is_eof()) {
    BeginFrame();
  }

  ~Framer() { FinishFrame(true, is_eof_); }

  void EmitAdvertiseTableSizeChange() {
    VarIntEncoder<3> enc(encoder_->max_table_size_, 0x20);
    enc.Write(AddTinyHeaderData(enc.length()));
  }

  // OnXXX functions, implementing header encoding via metadata::Collection
  // iteration (meaning that a Framer can be passed to
  // metadata::Collection::ForEachField)
  void OnPath(int path) { EncodeField(PathKV(path), &encoder_->path_index_); }
  void OnNamedKey(metadata::NamedKeys key, grpc_slice value) {
    EncodeField(NamedKV(key, value), &encoder_->namedkv_index_);
  }
  void OnStatus(int status) {
    CheckRegularHeaderOrdering(false);
    switch (status) {
      case 200:
        EmitIndexed(TableIndex(-8));  // static table element 8 is :status 200
        break;
      case 204:
        EmitIndexed(TableIndex(-9));
        break;
      case 206:
        EmitIndexed(TableIndex(-10));
        break;
      case 304:
        EmitIndexed(TableIndex(-11));
        break;
      case 400:
        EmitIndexed(TableIndex(-12));
        break;
      case 404:
        EmitIndexed(TableIndex(-13));
        break;
      case 500:
        EmitIndexed(TableIndex(-14));
        break;
      default: {
        char buffer[GPR_LTOA_MIN_BUFSIZE];
        gpr_ltoa(status, buffer);
        EmitIdxKeyLitValueNoAdd(TableIndex(-8), false,
                                grpc_slice_from_copied_string(buffer));
      }
    }
  }
  void OnMethod(metadata::HttpMethod method) {
    switch (method) {
      case metadata::HttpMethod::POST:
        EmitIndexed(TableIndex(-3));
        break;
      case metadata::HttpMethod::GET:
        EmitIndexed(TableIndex(-2));
        break;
      case metadata::HttpMethod::PUT:
        EncodeSpecialValue(TableIndex(-2), &encoder_->method_put_index_, false,
                           GRPC_MDSTR_METHOD, GRPC_MDSTR_PUT);
        break;
      case metadata::HttpMethod::UNKNOWN:
      case metadata::HttpMethod::UNSET:
        abort();
    }
  }
  void OnScheme(metadata::HttpScheme scheme) {
    switch (scheme) {
      case metadata::HttpScheme::HTTP:
        EmitIndexed(TableIndex(-6));
        break;
      case metadata::HttpScheme::HTTPS:
        EmitIndexed(TableIndex(-7));
        break;
      case metadata::HttpScheme::GRPC:
        EncodeSpecialValue(TableIndex(-6), &encoder_->scheme_grpc_index_, false,
                           GRPC_MDSTR_SCHEME, GRPC_MDSTR_GRPC);
        break;
      case metadata::HttpScheme::UNKNOWN:
      case metadata::HttpScheme::UNSET:
        abort();
    }
  }
  void OnTe(metadata::HttpTe te) {
    switch (te) {
      case metadata::HttpTe::TRAILERS:
        EncodeSpecialValue(TableIndex(), &encoder_->te_trailers_index_, false,
                           GRPC_MDSTR_TE, GRPC_MDSTR_TRAILERS);
        break;
      case metadata::HttpTe::UNKNOWN:
      case metadata::HttpTe::UNSET:
        abort();
    }
  }
  void OnContentType(metadata::ContentType ct) {
    switch (ct) {
      case metadata::ContentType::APPLICATION_SLASH_GRPC:
        EncodeSpecialValue(
            TableIndex(-31),
            &encoder_->content_type_application_slash_grpc_index_, false,
            GRPC_MDSTR_CONTENT_TYPE, GRPC_MDSTR_APPLICATION_SLASH_GRPC);
        break;
      case metadata::ContentType::UNKNOWN:
      case metadata::ContentType::UNSET:
        abort();
    }
  }

 private:
  template <class TKeyValue, class TIndex>
  void EncodeField(const TKeyValue& kv, TIndex* index) {
    CheckRegularHeaderOrdering(kv.IsRegularHeader());

    // if no compression possible, just send it literally
    if (!kv.AllowCompression()) {
      EmitLitKeyLitValueNoAdd(kv.KeySlice(), kv.ValueSlice());
      return;
    }

    // is the value already in the hpack table?
    IndexLookupResult lur_kv = index->LookupKeyValue(kv);
    if (lur_kv.idx.Exists(encoder_)) {
      EmitIndexed(lur_kv.idx);
      return;
    }

    IndexLookupResult lur_key = index->LookupKeyOnly(kv);
    if (lur_key.idx.Exists(encoder_)) {
      if (lur_kv.add && kv.SizeInHpackTable() <= MAX_DECODER_SPACE_USAGE) {
        EmitIdxKeyLitValueWithAdd(lur_key.idx, kv.IsBinaryHeader(),
                                  kv.ValueSlice());
        encoder_->AddToIndex(kv, index);
      } else {
        EmitIdxKeyLitValueNoAdd(lur_key.idx, kv.IsBinaryHeader(),
                                kv.ValueSlice());
      }
      return;
    }

    // maybe we should add the whole value just to add the key?
    // if the value is too big we should never try to add it to the table
    if (lur_key.add && kv.SizeInHpackTable() <= MAX_DECODER_SPACE_USAGE) {
      EmitLitKeyLitValueWithAdd(kv.KeySlice(), kv.ValueSlice());
      encoder_->AddToIndex(kv, index);
      return;
    }

    // finally, just send it literally
    EmitLitKeyLitValueNoAdd(kv.KeySlice(), kv.ValueSlice());
  }

  void EncodeSpecialValue(TableIndex key_index, TableIndex* elem_index,
                          bool is_bin, grpc_slice key, grpc_slice value) {
    if (elem_index->Exists(encoder_)) {
      EmitIndexed(*elem_index);
      return;
    }
    if (key_index.Exists(encoder_)) {
      EmitIdxKeyLitValueWithAdd(key_index, is_bin, value);
    } else {
      EmitLitKeyLitValueWithAdd(key, value);
    }
    *elem_index = encoder_->PrepareTableSpaceForNewElem(
        32 + GRPC_SLICE_LENGTH(key) + GRPC_SLICE_LENGTH(value));
  }

  void CheckRegularHeaderOrdering(bool is_regular_header) {
#ifndef NDEBUG
    if (is_regular_header) {
      seen_regular_header_ = true;
    } else {
      assert(!seen_regular_header_ &&
             "Reserved header (colon-prefixed) happening after regular ones.");
    }
#endif
  }

  /* begin a new frame: reserve off header space, remember how many bytes we'd
     output before beginning */
  void BeginFrame() {
    header_idx_ = grpc_slice_buffer_add_indexed(output_, GRPC_SLICE_MALLOC(9));
    output_length_at_start_of_frame_ = output_->length;
  }

  /* fills p (which is expected to be 9 bytes long) with a data frame header */
  static void FillHeader(uint8_t* p, uint8_t type, uint32_t id, size_t len,
                         uint8_t flags) {
    GPR_ASSERT(len < 16777316);
    *p++ = static_cast<uint8_t>(len >> 16);
    *p++ = static_cast<uint8_t>(len >> 8);
    *p++ = static_cast<uint8_t>(len);
    *p++ = type;
    *p++ = flags;
    *p++ = static_cast<uint8_t>(id >> 24);
    *p++ = static_cast<uint8_t>(id >> 16);
    *p++ = static_cast<uint8_t>(id >> 8);
    *p++ = static_cast<uint8_t>(id);
  }

  /* finish a frame - fill in the previously reserved header */
  void FinishFrame(bool is_header_boundary, bool is_last_in_stream) {
    uint8_t type = 0xff;
    type = is_first_frame_ ? GRPC_CHTTP2_FRAME_HEADER
                           : GRPC_CHTTP2_FRAME_CONTINUATION;
    FillHeader(
        GRPC_SLICE_START_PTR(output_->slices[header_idx_]), type, stream_id_,
        output_->length - output_length_at_start_of_frame_,
        (uint8_t)(
            (is_last_in_stream ? GRPC_CHTTP2_DATA_FLAG_END_STREAM : 0) |
            (is_header_boundary ? GRPC_CHTTP2_DATA_FLAG_END_HEADERS : 0)));
    stats_->framing_bytes += 9;
    is_first_frame_ = false;
  }

  void BeginNextFrame() {
    FinishFrame(false, false);
    BeginFrame();
  }

  /* make sure that the current frame is of the type desired, and has sufficient
     space to add at least about_to_add bytes -- finishes the current frame if
     needed */
  void EnsureSpace(size_t need_bytes) {
    if (output_->length - output_length_at_start_of_frame_ + need_bytes <=
        max_frame_size_) {
      return;
    }
    BeginNextFrame();
  }

  void AddHeaderData(grpc_slice slice) {
    size_t len = GRPC_SLICE_LENGTH(slice);
    size_t remaining;
    if (len == 0) return;
    remaining =
        max_frame_size_ + output_length_at_start_of_frame_ - output_->length;
    if (len <= remaining) {
      stats_->header_bytes += len;
      grpc_slice_buffer_add(output_, slice);
    } else {
      stats_->header_bytes += remaining;
      grpc_slice_buffer_add(output_, grpc_slice_split_head(&slice, remaining));
      BeginNextFrame();
      AddHeaderData(slice);
    }
  }

  uint8_t* AddTinyHeaderData(size_t len) {
    EnsureSpace(len);
    stats_->header_bytes += len;
    return grpc_slice_buffer_tiny_add(output_, len);
  }

  /****************************************************************************
   * Add multiple pieces of tiny header data... what follows is support
   * structure for AddTinyHeaderDataSegments()
   */
  template <class F>
  struct TinyHeaderDataSegment {
    size_t size;
    F add_func;
  };

  template <class F>
  TinyHeaderDataSegment<F> MakeTinyHeaderDataSegment(size_t size, F&& f) {
    return TinyHeaderDataSegment<F>{size, std::move(f)};
  }

  template <class F0>
  int SumSize(const TinyHeaderDataSegment<F0>& f0) {
    return f0.size;
  }

  template <class F0, class... Fs>
  int SumSize(const TinyHeaderDataSegment<F0>& f0,
              const TinyHeaderDataSegment<Fs>&... ps) {
    return f0.size + SumSize(ps...);
  }

  template <class F0>
  void RunFuncs(uint8_t* p, const TinyHeaderDataSegment<F0>& f0) {
    f0.add_func(p);
  }

  template <class F0, class... Fs>
  void RunFuncs(uint8_t* p, const TinyHeaderDataSegment<F0>& f0,
                const TinyHeaderDataSegment<Fs>&... ps) {
    f0.add_func(p);
    RunFuncs(p + f0.size, ps...);
  }

  template <class... Fs>
  void AddTinyHeaderDataSegments(const TinyHeaderDataSegment<Fs>&... fs) {
    RunFuncs(AddTinyHeaderData(SumSize(fs...)), fs...);
  }

  /****************************************************************************
   * Actual emission functions
   */
  void EmitLitKeyLitValueNoAdd(grpc_slice key, grpc_slice value) {
    GRPC_STATS_INC_HPACK_SEND_LITHDR_NOTIDX_V();
    GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
    VarIntEncoder<1> len_key_enc(static_cast<uint32_t>(GRPC_SLICE_LENGTH(key)),
                                 0x00);
    AddTinyHeaderDataSegments(
        MakeTinyHeaderDataSegment(1, [](uint8_t* p) { *p = 0x00; }),
        MakeTinyHeaderDataSegment(
            len_key_enc.length(),
            [&len_key_enc](uint8_t* p) { len_key_enc.Write(p); }));
    WireEncodedValue wire_value(value, grpc_is_binary_header(key),
                                use_true_binary_metadata_);
    VarIntEncoder<1> len_value_enc(wire_value.Length(),
                                   wire_value.huffman_prefix());
    AddHeaderData(grpc_slice_ref_internal(key));
    AddTinyHeaderDataSegments(
        MakeTinyHeaderDataSegment(
            len_value_enc.length(),
            [&len_value_enc](uint8_t* p) { len_value_enc.Write(p); }),
        MakeTinyHeaderDataSegment(
            wire_value.ValuePrefixLength(),
            [&wire_value](uint8_t* p) { wire_value.WriteValuePrefix(p); }));
    AddHeaderData(value);
  }

  void EmitLitKeyLitValueWithAdd(grpc_slice key, grpc_slice value) {
    GRPC_STATS_INC_HPACK_SEND_LITHDR_INCIDX_V();
    GRPC_STATS_INC_HPACK_SEND_UNCOMPRESSED();
    VarIntEncoder<1> len_key_enc(static_cast<uint32_t>(GRPC_SLICE_LENGTH(key)),
                                 0x00);
    AddTinyHeaderDataSegments(
        MakeTinyHeaderDataSegment(1, [](uint8_t* p) { *p = 0x40; }),
        MakeTinyHeaderDataSegment(
            len_key_enc.length(),
            [&len_key_enc](uint8_t* p) { len_key_enc.Write(p); }));
    WireEncodedValue wire_value(value, grpc_is_binary_header(key),
                                use_true_binary_metadata_);
    VarIntEncoder<1> len_value_enc(wire_value.Length(),
                                   wire_value.huffman_prefix());
    AddHeaderData(grpc_slice_ref_internal(key));
    AddTinyHeaderDataSegments(
        MakeTinyHeaderDataSegment(
            len_value_enc.length(),
            [&len_value_enc](uint8_t* p) { len_value_enc.Write(p); }),
        MakeTinyHeaderDataSegment(
            wire_value.ValuePrefixLength(),
            [&wire_value](uint8_t* p) { wire_value.WriteValuePrefix(p); }));
    AddHeaderData(value);
  }

  void EmitIdxKeyLitValueNoAdd(TableIndex idx, bool is_bin, grpc_slice value) {
    GRPC_STATS_INC_HPACK_SEND_LITHDR_NOTIDX();
    VarIntEncoder<4> key_enc(idx.WireValue(encoder_), 0x00);
    WireEncodedValue wire_value(value, is_bin, use_true_binary_metadata_);
    VarIntEncoder<1> len_enc(wire_value.Length(), wire_value.huffman_prefix());
    AddTinyHeaderDataSegments(
        MakeTinyHeaderDataSegment(key_enc.length(),
                                  [&key_enc](uint8_t* p) { key_enc.Write(p); }),
        MakeTinyHeaderDataSegment(len_enc.length(),
                                  [&len_enc](uint8_t* p) { len_enc.Write(p); }),
        MakeTinyHeaderDataSegment(
            wire_value.ValuePrefixLength(),
            [&wire_value](uint8_t* p) { wire_value.WriteValuePrefix(p); }));
    AddHeaderData(wire_value.value());
  }

  void EmitIdxKeyLitValueWithAdd(TableIndex idx, bool is_bin,
                                 grpc_slice value) {
    GRPC_STATS_INC_HPACK_SEND_LITHDR_INCIDX();
    VarIntEncoder<2> key_enc(idx.WireValue(encoder_), 0x40);
    WireEncodedValue wire_value(value, is_bin, use_true_binary_metadata_);
    VarIntEncoder<1> len_enc(wire_value.Length(), wire_value.huffman_prefix());
    AddTinyHeaderDataSegments(
        MakeTinyHeaderDataSegment(key_enc.length(),
                                  [&key_enc](uint8_t* p) { key_enc.Write(p); }),
        MakeTinyHeaderDataSegment(len_enc.length(),
                                  [&len_enc](uint8_t* p) { len_enc.Write(p); }),
        MakeTinyHeaderDataSegment(
            wire_value.ValuePrefixLength(),
            [&wire_value](uint8_t* p) { wire_value.WriteValuePrefix(p); }));
    AddHeaderData(wire_value.value());
  }

  void EmitIndexed(TableIndex idx) {
    GRPC_STATS_INC_HPACK_SEND_INDEXED();
    VarIntEncoder<1> idx_enc(idx.WireValue(encoder_), 0x80);
    idx_enc.Write(AddTinyHeaderData(idx_enc.length()));
  }

  HpackEncoder* const encoder_;
  bool is_first_frame_ = true;
  /* number of bytes in 'output' when we started the frame - used to calculate
     frame length */
  size_t output_length_at_start_of_frame_;
  /* index (in output) of the header for the current frame */
  size_t header_idx_;
  /* output stream id */
  const uint32_t stream_id_;
  grpc_slice_buffer* const output_;
  grpc_transport_one_way_stats* const stats_;
  /* maximum size of a frame */
  const size_t max_frame_size_;
  const bool use_true_binary_metadata_;
  const bool is_eof_;
#ifndef NDEBUG
  /* have we seen a regular (non-colon-prefixed) header yet? */
  bool seen_regular_header_ = false;
#endif
};

HpackEncoder::HpackEncoder() {
  table_elem_size_ = static_cast<uint16_t*>(
      gpr_malloc(sizeof(*table_elem_size_) * cap_table_elems_));
  memset(table_elem_size_, 0, sizeof(*table_elem_size_) * cap_table_elems_);
}

HpackEncoder::~HpackEncoder() { gpr_free(table_elem_size_); }

void HpackEncoder::EncodeHeader(uint32_t stream_id,
                                const metadata::Collection* md,
                                const FrameOptions& options,
                                grpc_slice_buffer* outbuf) {
  GPR_ASSERT(stream_id != 0);

  Framer framer(this, stream_id, options, outbuf);

  if (advertise_table_size_change_) {
    framer.EmitAdvertiseTableSizeChange();
    advertise_table_size_change_ = false;
  }

  /* Encode a metadata batch; store the returned values, representing
     a metadata element that needs to be unreffed back into the metadata
     slot. THIS MAY NOT BE THE SAME ELEMENT (if a decoder table slot got
     updated). After this loop, we'll do a batch unref of elements. */

  md->ForEachField(&framer);
}

void HpackEncoder::EvictOneEntry() {
  tail_remote_index_++;
  GPR_ASSERT(tail_remote_index_ > 0);
  GPR_ASSERT(table_size_ >=
             table_elem_size_[tail_remote_index_ % cap_table_elems_]);
  GPR_ASSERT(table_elems_ > 0);
  table_size_ = (uint16_t)(
      table_size_ - table_elem_size_[tail_remote_index_ % cap_table_elems_]);
  table_elems_--;
}

// Reserve space in table for the new element, evict entries if needed.
// Return the new index of the element. Return 0 to indicate not adding to
// table.
HpackEncoder::TableIndex HpackEncoder::PrepareTableSpaceForNewElem(
    size_t elem_size) {
  int new_index = tail_remote_index_ + table_elems_ + 1;
  GPR_ASSERT(elem_size < 65536);

  if (elem_size > max_table_size_) {
    while (table_size_ > 0) {
      EvictOneEntry();
    }
    return TableIndex();
  }

  /* Reserve space for this element in the remote table: if this overflows
     the current table, drop elements until it fits, matching the decompressor
     algorithm */
  while (table_size_ + elem_size > max_table_size_) {
    EvictOneEntry();
  }
  GPR_ASSERT(table_elems_ < max_table_size_);
  table_elem_size_[new_index % cap_table_elems_] = (uint16_t)elem_size;
  table_size_ = (uint16_t)(table_size_ + elem_size);
  table_elems_++;

  return TableIndex(new_index);
}

void HpackEncoder::SetMaxUsableSize(uint32_t max_table_size) {
  max_usable_size_ = max_table_size;
  SetMaxTableSize(std::min(max_table_size_, max_table_size));
}

void HpackEncoder::SetMaxTableSize(uint32_t max_table_size) {
  max_table_size = std::min(max_table_size, max_usable_size_);
  if (max_table_size == max_table_size_) {
    return;
  }
  while (table_size_ > 0 && table_size_ > max_table_size) {
    EvictOneEntry();
  }
  max_table_size_ = max_table_size;
  max_table_elems_ = ElemsForBytes(max_table_size);
  if (max_table_elems_ > cap_table_elems_) {
    RebuildElems(std::max(max_table_elems_, 2 * cap_table_elems_));
  } else if (max_table_elems_ < cap_table_elems_ / 3) {
    uint32_t new_cap = std::max(max_table_elems_, uint32_t(16));
    if (new_cap != cap_table_elems_) {
      RebuildElems(new_cap);
    }
  }
  advertise_table_size_change_ = true;
  if (grpc_http_trace.enabled()) {
    gpr_log(GPR_DEBUG, "set max table size from encoder to %d", max_table_size);
  }
}

void HpackEncoder::RebuildElems(uint32_t new_cap) {
  uint16_t* table_elem_size =
      static_cast<uint16_t*>(gpr_malloc(sizeof(*table_elem_size) * new_cap));
  uint32_t i;

  memset(table_elem_size, 0, sizeof(*table_elem_size) * new_cap);
  GPR_ASSERT(table_elems_ <= new_cap);

  for (i = 0; i < table_elems_; i++) {
    uint32_t ofs = tail_remote_index_ + i + 1;
    table_elem_size[ofs % new_cap] = table_elem_size_[ofs % cap_table_elems_];
  }

  cap_table_elems_ = new_cap;
  gpr_free(table_elem_size_);
  table_elem_size_ = table_elem_size;
}

}  // namespace chttp2
}  // namespace grpc_core

#if 0
#define STRLEN_LIT(x) (sizeof(x) - 1)
#define TIMEOUT_KEY "grpc-timeout"

static void deadline_enc(grpc_chttp2_hpack_compressor* c, grpc_millis deadline,
                         framer_state* st) {
  char timeout_str[GRPC_HTTP2_TIMEOUT_ENCODE_MIN_BUFSIZE];
  grpc_mdelem mdelem;
  grpc_http2_encode_timeout(deadline - grpc_core::ExecCtx::Get()->Now(),
                            timeout_str);
  mdelem = grpc_mdelem_from_slices(GRPC_MDSTR_GRPC_TIMEOUT,
                                   grpc_slice_from_copied_string(timeout_str));
  hpack_enc(c, mdelem, st);
  GRPC_MDELEM_UNREF(mdelem);
}
#endif
