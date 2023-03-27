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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"

#include <stddef.h>
#include <stdlib.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"

#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/decode_huff.h"
#include "src/core/ext/transport/chttp2/transport/hpack_constants.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/debug/stats_data.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_refcount.h"
#include "src/core/lib/transport/parsed_metadata.h"

// IWYU pragma: no_include <type_traits>

namespace grpc_core {

TraceFlag grpc_trace_chttp2_hpack_parser(false, "chttp2_hpack_parser");

namespace {
// The alphabet used for base64 encoding binary metadata.
constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

// An inverted table: for each value in kBase64Alphabet, table contains the
// index with which it's stored, so we can quickly invert the encoding without
// any complicated runtime logic.
struct Base64InverseTable {
  uint8_t table[256]{};
  constexpr Base64InverseTable() {
    for (int i = 0; i < 256; i++) {
      table[i] = 255;
    }
    for (const char* p = kBase64Alphabet; *p; p++) {
      uint8_t idx = *p;
      uint8_t ofs = p - kBase64Alphabet;
      table[idx] = ofs;
    }
  }
};

constexpr Base64InverseTable kBase64InverseTable;
}  // namespace

// Input tracks the current byte through the input data and provides it
// via a simple stream interface.
class HPackParser::Input {
 public:
  Input(grpc_slice_refcount* current_slice_refcount, const uint8_t* begin,
        const uint8_t* end)
      : current_slice_refcount_(current_slice_refcount),
        begin_(begin),
        end_(end),
        frontier_(begin) {}

  // If input is backed by a slice, retrieve its refcount. If not, return
  // nullptr.
  grpc_slice_refcount* slice_refcount() { return current_slice_refcount_; }

  // Have we reached the end of input?
  bool end_of_stream() const { return begin_ == end_; }
  // How many bytes until end of input
  size_t remaining() const { return end_ - begin_; }
  // Current position, as a pointer
  const uint8_t* cur_ptr() const { return begin_; }
  // End position, as a pointer
  const uint8_t* end_ptr() const { return end_; }
  // Move read position forward by n, unchecked
  void Advance(size_t n) { begin_ += n; }

  // Retrieve the current character, or nullopt if end of stream
  // Do not advance
  absl::optional<uint8_t> peek() const {
    if (end_of_stream()) {
      return {};
    }
    return *begin_;
  }

  // Retrieve and advance past the current character, or return nullopt if end
  // of stream
  absl::optional<uint8_t> Next() {
    if (end_of_stream()) {
      return UnexpectedEOF(absl::optional<uint8_t>());
    }
    return *begin_++;
  }

  // Helper to parse a varint delta on top of value, return nullopt on failure
  // (setting error)
  absl::optional<uint32_t> ParseVarint(uint32_t value) {
    // TODO(ctiller): break out a variant of this when we know there are at
    // least 5 bytes in input_
    auto cur = Next();
    if (!cur) return {};
    value += *cur & 0x7f;
    if ((*cur & 0x80) == 0) return value;

    cur = Next();
    if (!cur) return {};
    value += (*cur & 0x7f) << 7;
    if ((*cur & 0x80) == 0) return value;

    cur = Next();
    if (!cur) return {};
    value += (*cur & 0x7f) << 14;
    if ((*cur & 0x80) == 0) return value;

    cur = Next();
    if (!cur) return {};
    value += (*cur & 0x7f) << 21;
    if ((*cur & 0x80) == 0) return value;

    cur = Next();
    if (!cur) return {};
    uint32_t c = (*cur) & 0x7f;
    // We might overflow here, so we need to be a little careful about the
    // addition
    if (c > 0xf) return ParseVarintOutOfRange(value, *cur);
    const uint32_t add = c << 28;
    if (add > 0xffffffffu - value) {
      return ParseVarintOutOfRange(value, *cur);
    }
    value += add;
    if ((*cur & 0x80) == 0) return value;

    // Spec weirdness: we can add an infinite stream of 0x80 at the end of a
    // varint and still end up with a correctly encoded varint.
    do {
      cur = Next();
      if (!cur.has_value()) return {};
    } while (*cur == 0x80);

    // BUT... the last byte needs to be 0x00 or we'll overflow dramatically!
    if (*cur == 0) return value;
    return ParseVarintOutOfRange(value, *cur);
  }

  // Prefix for a string
  struct StringPrefix {
    // Number of bytes in input for string
    uint32_t length;
    // Is it huffman compressed
    bool huff;
  };

  // Parse a string prefix
  absl::optional<StringPrefix> ParseStringPrefix() {
    auto cur = Next();
    if (!cur.has_value()) return {};
    // Huffman if the top bit is 1
    const bool huff = (*cur & 0x80) != 0;
    // String length
    uint32_t strlen = (*cur & 0x7f);
    if (strlen == 0x7f) {
      // all ones ==> varint string length
      auto v = ParseVarint(0x7f);
      if (!v.has_value()) return {};
      strlen = *v;
    }
    return StringPrefix{strlen, huff};
  }

  // Check if we saw an EOF.. must be verified before looking at TakeError
  bool eof_error() const { return eof_error_; }

  // Extract the parse error, leaving the current error as NONE.
  grpc_error_handle TakeError() {
    grpc_error_handle out = error_;
    error_ = absl::OkStatus();
    return out;
  }

  // Set the current error - allows the rest of the code not to need to pass
  // around StatusOr<> which would be prohibitive here.
  GPR_ATTRIBUTE_NOINLINE void SetError(grpc_error_handle error) {
    if (!error_.ok() || eof_error_) {
      return;
    }
    error_ = error;
    begin_ = end_;
  }

  // If no error is set, set it to the value produced by error_factory.
  // Return return_value unchanged.
  template <typename F, typename T>
  GPR_ATTRIBUTE_NOINLINE T MaybeSetErrorAndReturn(F error_factory,
                                                  T return_value) {
    if (!error_.ok() || eof_error_) return return_value;
    error_ = error_factory();
    begin_ = end_;
    return return_value;
  }

  // Set the error to an unexpected eof, and return result (code golfed as this
  // is a common case)
  template <typename T>
  T UnexpectedEOF(T return_value) {
    if (!error_.ok()) return return_value;
    eof_error_ = true;
    return return_value;
  }

  // Update the frontier - signifies we've successfully parsed another element
  void UpdateFrontier() { frontier_ = begin_; }

  // Get the frontier - for buffering should we fail due to eof
  const uint8_t* frontier() const { return frontier_; }

 private:
  // Helper to set the error to out of range for ParseVarint
  absl::optional<uint32_t> ParseVarintOutOfRange(uint32_t value,
                                                 uint8_t last_byte) {
    return MaybeSetErrorAndReturn(
        [value, last_byte] {
          return GRPC_ERROR_CREATE(absl::StrFormat(
              "integer overflow in hpack integer decoding: have 0x%08x, "
              "got byte 0x%02x on byte 5",
              value, last_byte));
        },
        absl::optional<uint32_t>());
  }

  // Refcount if we are backed by a slice
  grpc_slice_refcount* current_slice_refcount_;
  // Current input point
  const uint8_t* begin_;
  // End of stream point
  const uint8_t* const end_;
  // Frontier denotes the first byte past successfully processed input
  const uint8_t* frontier_;
  // Current error
  grpc_error_handle error_;
  // If the error was EOF, we flag it here..
  bool eof_error_ = false;
};

// Helper to parse a string and turn it into a slice with appropriate memory
// management characteristics
class HPackParser::String {
 public:
  String(const String&) = delete;
  String& operator=(const String&) = delete;
  String(String&& other) noexcept : value_(std::move(other.value_)) {
    other.value_ = absl::Span<const uint8_t>();
  }
  String& operator=(String&& other) noexcept {
    value_ = std::move(other.value_);
    other.value_ = absl::Span<const uint8_t>();
    return *this;
  }

  // Take the value and leave this empty
  Slice Take();

  // Return a reference to the value as a string view
  absl::string_view string_view() const {
    if (auto* p = absl::get_if<Slice>(&value_)) {
      return p->as_string_view();
    } else if (auto* p = absl::get_if<absl::Span<const uint8_t>>(&value_)) {
      return absl::string_view(reinterpret_cast<const char*>(p->data()),
                               p->size());
    } else if (auto* p = absl::get_if<std::vector<uint8_t>>(&value_)) {
      return absl::string_view(reinterpret_cast<const char*>(p->data()),
                               p->size());
    }
    GPR_UNREACHABLE_CODE(return absl::string_view());
  }

  // Parse a non-binary string
  static absl::optional<String> Parse(Input* input) {
    auto pfx = input->ParseStringPrefix();
    if (!pfx.has_value()) return {};
    if (pfx->huff) {
      // Huffman coded
      std::vector<uint8_t> output;
      auto v = ParseHuff(input, pfx->length,
                         [&output](uint8_t c) { output.push_back(c); });
      if (!v) return {};
      return String(std::move(output));
    }
    return ParseUncompressed(input, pfx->length);
  }

  // Parse a binary string
  static absl::optional<String> ParseBinary(Input* input) {
    auto pfx = input->ParseStringPrefix();
    if (!pfx.has_value()) return {};
    if (!pfx->huff) {
      if (pfx->length > 0 && input->peek() == 0) {
        // 'true-binary'
        input->Advance(1);
        return ParseUncompressed(input, pfx->length - 1);
      }
      // Base64 encoded... pull out the string, then unbase64 it
      auto base64 = ParseUncompressed(input, pfx->length);
      if (!base64.has_value()) return {};
      return Unbase64(input, std::move(*base64));
    } else {
      // Huffman encoded...
      std::vector<uint8_t> decompressed;
      // State here says either we don't know if it's base64 or binary, or we do
      // and what is it.
      enum class State { kUnsure, kBinary, kBase64 };
      State state = State::kUnsure;
      auto decompressed_ok =
          ParseHuff(input, pfx->length, [&state, &decompressed](uint8_t c) {
            if (state == State::kUnsure) {
              // First byte... if it's zero it's binary
              if (c == 0) {
                // Save the type, and skip the zero
                state = State::kBinary;
                return;
              } else {
                // Flag base64, store this value
                state = State::kBase64;
              }
            }
            // Non-first byte, or base64 first byte
            decompressed.push_back(c);
          });
      if (!decompressed_ok) return {};
      switch (state) {
        case State::kUnsure:
          // No bytes, empty span
          return String(absl::Span<const uint8_t>());
        case State::kBinary:
          // Binary, we're done
          return String(std::move(decompressed));
        case State::kBase64:
          // Base64 - unpack it
          return Unbase64(input, String(std::move(decompressed)));
      }
      GPR_UNREACHABLE_CODE(abort(););
    }
  }

 private:
  void AppendBytes(const uint8_t* data, size_t length);
  explicit String(std::vector<uint8_t> v) : value_(std::move(v)) {}
  explicit String(absl::Span<const uint8_t> v) : value_(v) {}
  String(grpc_slice_refcount* r, const uint8_t* begin, const uint8_t* end)
      : value_(Slice::FromRefcountAndBytes(r, begin, end)) {}

  // Parse some huffman encoded bytes, using output(uint8_t b) to emit each
  // decoded byte.
  template <typename Out>
  static bool ParseHuff(Input* input, uint32_t length, Out output) {
    // If there's insufficient bytes remaining, return now.
    if (input->remaining() < length) {
      return input->UnexpectedEOF(false);
    }
    // Grab the byte range, and iterate through it.
    const uint8_t* p = input->cur_ptr();
    input->Advance(length);
    return HuffDecoder<Out>(output, p, p + length).Run();
  }

  // Parse some uncompressed string bytes.
  static absl::optional<String> ParseUncompressed(Input* input,
                                                  uint32_t length) {
    // Check there's enough bytes
    if (input->remaining() < length) {
      return input->UnexpectedEOF(absl::optional<String>());
    }
    auto* refcount = input->slice_refcount();
    auto* p = input->cur_ptr();
    input->Advance(length);
    if (refcount != nullptr) {
      return String(refcount, p, p + length);
    } else {
      return String(absl::Span<const uint8_t>(p, length));
    }
  }

  // Turn base64 encoded bytes into not base64 encoded bytes.
  // Only takes input to set an error on failure.
  static absl::optional<String> Unbase64(Input* input, String s) {
    absl::optional<std::vector<uint8_t>> result;
    if (auto* p = absl::get_if<Slice>(&s.value_)) {
      result = Unbase64Loop(p->begin(), p->end());
    }
    if (auto* p = absl::get_if<absl::Span<const uint8_t>>(&s.value_)) {
      result = Unbase64Loop(p->begin(), p->end());
    }
    if (auto* p = absl::get_if<std::vector<uint8_t>>(&s.value_)) {
      result = Unbase64Loop(p->data(), p->data() + p->size());
    }
    if (!result.has_value()) {
      return input->MaybeSetErrorAndReturn(
          [] { return GRPC_ERROR_CREATE("illegal base64 encoding"); },
          absl::optional<String>());
    }
    return String(std::move(*result));
  }

  // Main loop for Unbase64
  static absl::optional<std::vector<uint8_t>> Unbase64Loop(const uint8_t* cur,
                                                           const uint8_t* end) {
    while (cur != end && end[-1] == '=') {
      --end;
    }

    std::vector<uint8_t> out;
    out.reserve(3 * (end - cur) / 4 + 3);

    // Decode 4 bytes at a time while we can
    while (end - cur >= 4) {
      uint32_t bits = kBase64InverseTable.table[*cur];
      if (bits > 63) return {};
      uint32_t buffer = bits << 18;
      ++cur;

      bits = kBase64InverseTable.table[*cur];
      if (bits > 63) return {};
      buffer |= bits << 12;
      ++cur;

      bits = kBase64InverseTable.table[*cur];
      if (bits > 63) return {};
      buffer |= bits << 6;
      ++cur;

      bits = kBase64InverseTable.table[*cur];
      if (bits > 63) return {};
      buffer |= bits;
      ++cur;

      out.insert(out.end(), {static_cast<uint8_t>(buffer >> 16),
                             static_cast<uint8_t>(buffer >> 8),
                             static_cast<uint8_t>(buffer)});
    }
    // Deal with the last 0, 1, 2, or 3 bytes.
    switch (end - cur) {
      case 0:
        return out;
      case 1:
        return {};
      case 2: {
        uint32_t bits = kBase64InverseTable.table[*cur];
        if (bits > 63) return {};
        uint32_t buffer = bits << 18;

        ++cur;
        bits = kBase64InverseTable.table[*cur];
        if (bits > 63) return {};
        buffer |= bits << 12;

        if (buffer & 0xffff) return {};
        out.push_back(static_cast<uint8_t>(buffer >> 16));
        return out;
      }
      case 3: {
        uint32_t bits = kBase64InverseTable.table[*cur];
        if (bits > 63) return {};
        uint32_t buffer = bits << 18;

        ++cur;
        bits = kBase64InverseTable.table[*cur];
        if (bits > 63) return {};
        buffer |= bits << 12;

        ++cur;
        bits = kBase64InverseTable.table[*cur];
        if (bits > 63) return {};
        buffer |= bits << 6;

        ++cur;
        if (buffer & 0xff) return {};
        out.push_back(static_cast<uint8_t>(buffer >> 16));
        out.push_back(static_cast<uint8_t>(buffer >> 8));
        return out;
      }
    }

    GPR_UNREACHABLE_CODE(return out;);
  }

  absl::variant<Slice, absl::Span<const uint8_t>, std::vector<uint8_t>> value_;
};

// Parser parses one key/value pair from a byte stream.
class HPackParser::Parser {
 public:
  Parser(Input* input, grpc_metadata_batch* metadata_buffer,
         uint32_t metadata_size_limit, HPackTable* table,
         uint8_t* dynamic_table_updates_allowed, uint32_t* frame_length,
         LogInfo log_info)
      : input_(input),
        metadata_buffer_(metadata_buffer),
        table_(table),
        dynamic_table_updates_allowed_(dynamic_table_updates_allowed),
        frame_length_(frame_length),
        metadata_size_limit_(metadata_size_limit),
        log_info_(log_info) {}

  // Skip any priority bits, or return false on failure
  bool SkipPriority() {
    if (input_->remaining() < 5) return input_->UnexpectedEOF(false);
    input_->Advance(5);
    return true;
  }

  bool Parse() {
    auto cur = *input_->Next();
    switch (cur >> 4) {
        // Literal header not indexed - First byte format: 0000xxxx
        // Literal header never indexed - First byte format: 0001xxxx
        // Where xxxx:
        //   0000  - literal key
        //   1111  - indexed key, varint encoded index
        //   other - indexed key, inline encoded index
      case 0:
      case 1:
        switch (cur & 0xf) {
          case 0:  // literal key
            return FinishHeaderOmitFromTable(ParseLiteralKey());
          case 0xf:  // varint encoded key index
            return FinishHeaderOmitFromTable(ParseVarIdxKey(0xf));
          default:  // inline encoded key index
            return FinishHeaderOmitFromTable(ParseIdxKey(cur & 0xf));
        }
        // Update max table size.
        // First byte format: 001xxxxx
        // Where xxxxx:
        //   11111 - max size is varint encoded
        //   other - max size is stored inline
      case 2:
        // inline encoded max table size
        return FinishMaxTableSize(cur & 0x1f);
      case 3:
        if (cur == 0x3f) {
          // varint encoded max table size
          return FinishMaxTableSize(input_->ParseVarint(0x1f));
        } else {
          // inline encoded max table size
          return FinishMaxTableSize(cur & 0x1f);
        }
        // Literal header with incremental indexing.
        // First byte format: 01xxxxxx
        // Where xxxxxx:
        //   000000 - literal key
        //   111111 - indexed key, varint encoded index
        //   other  - indexed key, inline encoded index
      case 4:
        if (cur == 0x40) {
          // literal key
          return FinishHeaderAndAddToTable(ParseLiteralKey());
        }
        ABSL_FALLTHROUGH_INTENDED;
      case 5:
      case 6:
        // inline encoded key index
        return FinishHeaderAndAddToTable(ParseIdxKey(cur & 0x3f));
      case 7:
        if (cur == 0x7f) {
          // varint encoded key index
          return FinishHeaderAndAddToTable(ParseVarIdxKey(0x3f));
        } else {
          // inline encoded key index
          return FinishHeaderAndAddToTable(ParseIdxKey(cur & 0x3f));
        }
        // Indexed Header Field Representation
        // First byte format: 1xxxxxxx
        // Where xxxxxxx:
        //   0000000 - illegal
        //   1111111 - varint encoded field index
        //   other   - inline encoded field index
      case 8:
        if (cur == 0x80) {
          // illegal value.
          return input_->MaybeSetErrorAndReturn(
              [] { return GRPC_ERROR_CREATE("Illegal hpack op code"); }, false);
        }
        ABSL_FALLTHROUGH_INTENDED;
      case 9:
      case 10:
      case 11:
      case 12:
      case 13:
      case 14:
        // inline encoded field index
        return FinishIndexed(cur & 0x7f);
      case 15:
        if (cur == 0xff) {
          // varint encoded field index
          return FinishIndexed(input_->ParseVarint(0x7f));
        } else {
          // inline encoded field index
          return FinishIndexed(cur & 0x7f);
        }
    }
    GPR_UNREACHABLE_CODE(abort());
  }

 private:
  void GPR_ATTRIBUTE_NOINLINE LogHeader(const HPackTable::Memento& memento) {
    const char* type;
    switch (log_info_.type) {
      case LogInfo::kHeaders:
        type = "HDR";
        break;
      case LogInfo::kTrailers:
        type = "TRL";
        break;
      case LogInfo::kDontKnow:
        type = "???";
        break;
    }
    gpr_log(GPR_DEBUG, "HTTP:%d:%s:%s: %s", log_info_.stream_id, type,
            log_info_.is_client ? "CLI" : "SVR", memento.DebugString().c_str());
  }

  bool EmitHeader(const HPackTable::Memento& md) {
    // Pass up to the transport
    if (GPR_UNLIKELY(metadata_buffer_ == nullptr)) return true;
    *frame_length_ += md.transport_size();
    if (GPR_UNLIKELY(*frame_length_ > metadata_size_limit_)) {
      return HandleMetadataSizeLimitExceeded(md);
    }

    metadata_buffer_->Set(md);
    return true;
  }

  bool FinishHeaderAndAddToTable(absl::optional<HPackTable::Memento> md) {
    // Allow higher code to just pass in failures ... simplifies things a bit.
    if (!md.has_value()) return false;
    // Log if desired
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_chttp2_hpack_parser)) {
      LogHeader(*md);
    }
    // Emit whilst we own the metadata.
    auto r = EmitHeader(*md);
    // Add to the hpack table
    grpc_error_handle err = table_->Add(std::move(*md));
    if (GPR_UNLIKELY(!err.ok())) {
      input_->SetError(err);
      return false;
    };
    return r;
  }

  bool FinishHeaderOmitFromTable(absl::optional<HPackTable::Memento> md) {
    // Allow higher code to just pass in failures ... simplifies things a bit.
    if (!md.has_value()) return false;
    return FinishHeaderOmitFromTable(*md);
  }

  bool FinishHeaderOmitFromTable(const HPackTable::Memento& md) {
    // Log if desired
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_chttp2_hpack_parser)) {
      LogHeader(md);
    }
    return EmitHeader(md);
  }

  // Parse a string encoded key and a string encoded value
  absl::optional<HPackTable::Memento> ParseLiteralKey() {
    auto key = String::Parse(input_);
    if (!key.has_value()) return {};
    auto value = ParseValueString(absl::EndsWith(key->string_view(), "-bin"));
    if (GPR_UNLIKELY(!value.has_value())) {
      return {};
    }
    auto key_string = key->string_view();
    auto value_slice = value->Take();
    const auto transport_size = key_string.size() + value_slice.size() +
                                hpack_constants::kEntryOverhead;
    return grpc_metadata_batch::Parse(
        key->string_view(), std::move(value_slice), transport_size,
        [key_string](absl::string_view error, const Slice& value) {
          ReportMetadataParseError(key_string, error, value.as_string_view());
        });
  }

  // Parse an index encoded key and a string encoded value
  absl::optional<HPackTable::Memento> ParseIdxKey(uint32_t index) {
    const auto* elem = table_->Lookup(index);
    if (GPR_UNLIKELY(elem == nullptr)) {
      return InvalidHPackIndexError(index,
                                    absl::optional<HPackTable::Memento>());
    }
    auto value = ParseValueString(elem->is_binary_header());
    if (GPR_UNLIKELY(!value.has_value())) return {};
    return elem->WithNewValue(
        value->Take(), [=](absl::string_view error, const Slice& value) {
          ReportMetadataParseError(elem->key(), error, value.as_string_view());
        });
  }

  // Parse a varint index encoded key and a string encoded value
  absl::optional<HPackTable::Memento> ParseVarIdxKey(uint32_t offset) {
    auto index = input_->ParseVarint(offset);
    if (GPR_UNLIKELY(!index.has_value())) return {};
    return ParseIdxKey(*index);
  }

  // Parse a string, figuring out if it's binary or not by the key name.
  absl::optional<String> ParseValueString(bool is_binary) {
    if (is_binary) {
      return String::ParseBinary(input_);
    } else {
      return String::Parse(input_);
    }
  }

  // Emit an indexed field
  bool FinishIndexed(absl::optional<uint32_t> index) {
    *dynamic_table_updates_allowed_ = 0;
    if (!index.has_value()) return false;
    const auto* elem = table_->Lookup(*index);
    if (GPR_UNLIKELY(elem == nullptr)) {
      return InvalidHPackIndexError(*index, false);
    }
    return FinishHeaderOmitFromTable(*elem);
  }

  // finish parsing a max table size change
  bool FinishMaxTableSize(absl::optional<uint32_t> size) {
    if (!size.has_value()) return false;
    if (*dynamic_table_updates_allowed_ == 0) {
      return input_->MaybeSetErrorAndReturn(
          [] {
            return GRPC_ERROR_CREATE(
                "More than two max table size changes in a single frame");
          },
          false);
    }
    (*dynamic_table_updates_allowed_)--;
    grpc_error_handle err = table_->SetCurrentTableSize(*size);
    if (!err.ok()) {
      input_->SetError(err);
      return false;
    }
    return true;
  }

  // Set an invalid hpack index error if no error has been set. Returns result
  // unmodified.
  template <typename R>
  R InvalidHPackIndexError(uint32_t index, R result) {
    return input_->MaybeSetErrorAndReturn(
        [this, index] {
          return grpc_error_set_int(
              grpc_error_set_int(
                  GRPC_ERROR_CREATE("Invalid HPACK index received"),
                  StatusIntProperty::kIndex, static_cast<intptr_t>(index)),
              StatusIntProperty::kSize,
              static_cast<intptr_t>(this->table_->num_entries()));
        },
        std::move(result));
  }

  class MetadataSizeLimitExceededEncoder {
   public:
    explicit MetadataSizeLimitExceededEncoder(std::string& summary)
        : summary_(summary) {}

    void Encode(const Slice& key, const Slice& value) {
      AddToSummary(key.as_string_view(), value.size());
    }

    template <typename Key, typename Value>
    void Encode(Key, const Value& value) {
      AddToSummary(Key::key(), EncodedSizeOfKey(Key(), value));
    }

   private:
    void AddToSummary(absl::string_view key,
                      size_t value_length) GPR_ATTRIBUTE_NOINLINE {
      absl::StrAppend(&summary_, " ", key, ":",
                      hpack_constants::SizeForEntry(key.size(), value_length),
                      "B");
    }
    std::string& summary_;
  };

  GPR_ATTRIBUTE_NOINLINE
  bool HandleMetadataSizeLimitExceeded(const HPackTable::Memento& md) {
    // Collect a summary of sizes so far for debugging
    // Do not collect contents, for fear of exposing PII.
    std::string summary;
    if (metadata_buffer_ != nullptr) {
      MetadataSizeLimitExceededEncoder encoder(summary);
      metadata_buffer_->Encode(&encoder);
    }
    summary =
        absl::StrCat("; adding ", md.key(), " (length ", md.transport_size(),
                     "B)", summary.empty() ? "" : " to ", summary);
    if (metadata_buffer_ != nullptr) metadata_buffer_->Clear();
    // StreamId is used as a signal to skip this stream but keep the connection
    // alive
    return input_->MaybeSetErrorAndReturn(
        [this, summary = std::move(summary)] {
          return grpc_error_set_int(
              grpc_error_set_int(
                  GRPC_ERROR_CREATE(absl::StrCat(
                      "received initial metadata size exceeds limit (",
                      *frame_length_, " vs. ", metadata_size_limit_, ")",
                      summary)),
                  StatusIntProperty::kRpcStatus,
                  GRPC_STATUS_RESOURCE_EXHAUSTED),
              StatusIntProperty::kStreamId, 0);
        },
        false);
  }

  static void ReportMetadataParseError(absl::string_view key,
                                       absl::string_view error,
                                       absl::string_view value) {
    gpr_log(
        GPR_ERROR, "Error parsing metadata: %s",
        absl::StrCat("error=", error, " key=", key, " value=", value).c_str());
  }

  Input* const input_;
  grpc_metadata_batch* const metadata_buffer_;
  HPackTable* const table_;
  uint8_t* const dynamic_table_updates_allowed_;
  uint32_t* const frame_length_;
  const uint32_t metadata_size_limit_;
  const LogInfo log_info_;
};

Slice HPackParser::String::Take() {
  if (auto* p = absl::get_if<Slice>(&value_)) {
    return p->Copy();
  } else if (auto* p = absl::get_if<absl::Span<const uint8_t>>(&value_)) {
    return Slice::FromCopiedBuffer(*p);
  } else if (auto* p = absl::get_if<std::vector<uint8_t>>(&value_)) {
    return Slice::FromCopiedBuffer(*p);
  }
  GPR_UNREACHABLE_CODE(return Slice());
}

// PUBLIC INTERFACE

HPackParser::HPackParser() = default;

HPackParser::~HPackParser() = default;

void HPackParser::BeginFrame(grpc_metadata_batch* metadata_buffer,
                             uint32_t metadata_size_limit, Boundary boundary,
                             Priority priority, LogInfo log_info) {
  metadata_buffer_ = metadata_buffer;
  if (metadata_buffer != nullptr) {
    metadata_buffer->Set(GrpcStatusFromWire(), true);
  }
  boundary_ = boundary;
  priority_ = priority;
  dynamic_table_updates_allowed_ = 2;
  frame_length_ = 0;
  metadata_size_limit_ = metadata_size_limit;
  log_info_ = log_info;
}

grpc_error_handle HPackParser::Parse(const grpc_slice& slice, bool is_last) {
  if (GPR_UNLIKELY(!unparsed_bytes_.empty())) {
    std::vector<uint8_t> buffer = std::move(unparsed_bytes_);
    buffer.insert(buffer.end(), GRPC_SLICE_START_PTR(slice),
                  GRPC_SLICE_END_PTR(slice));
    return ParseInput(
        Input(nullptr, buffer.data(), buffer.data() + buffer.size()), is_last);
  }
  return ParseInput(Input(slice.refcount, GRPC_SLICE_START_PTR(slice),
                          GRPC_SLICE_END_PTR(slice)),
                    is_last);
}

grpc_error_handle HPackParser::ParseInput(Input input, bool is_last) {
  bool parsed_ok = ParseInputInner(&input);
  if (is_last) global_stats().IncrementHttp2MetadataSize(frame_length_);
  if (parsed_ok) return absl::OkStatus();
  if (input.eof_error()) {
    if (GPR_UNLIKELY(is_last && is_boundary())) {
      return GRPC_ERROR_CREATE(
          "Incomplete header at the end of a header/continuation sequence");
    }
    unparsed_bytes_ = std::vector<uint8_t>(input.frontier(), input.end_ptr());
    return absl::OkStatus();
  }
  return input.TakeError();
}

bool HPackParser::ParseInputInner(Input* input) {
  switch (priority_) {
    case Priority::None:
      break;
    case Priority::Included: {
      if (input->remaining() < 5) return input->UnexpectedEOF(false);
      input->Advance(5);
      input->UpdateFrontier();
      priority_ = Priority::None;
    }
  }
  while (!input->end_of_stream()) {
    if (GPR_UNLIKELY(!Parser(input, metadata_buffer_, metadata_size_limit_,
                             &table_, &dynamic_table_updates_allowed_,
                             &frame_length_, log_info_)
                          .Parse())) {
      return false;
    }
    input->UpdateFrontier();
  }
  return true;
}

void HPackParser::FinishFrame() { metadata_buffer_ = nullptr; }

}  // namespace grpc_core
