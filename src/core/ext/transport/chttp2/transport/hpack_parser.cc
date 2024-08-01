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

#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"

#include <stddef.h>
#include <stdlib.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"

#include <grpc/slice.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/decode_huff.h"
#include "src/core/ext/transport/chttp2/transport/hpack_constants.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parse_result.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser_table.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_refcount.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/metadata_info.h"
#include "src/core/lib/transport/parsed_metadata.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"

// IWYU pragma: no_include <type_traits>

namespace grpc_core {

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
        const uint8_t* end, absl::BitGenRef bitsrc,
        HpackParseResult& frame_error, HpackParseResult& field_error)
      : current_slice_refcount_(current_slice_refcount),
        begin_(begin),
        end_(end),
        frontier_(begin),
        frame_error_(frame_error),
        field_error_(field_error),
        bitsrc_(bitsrc) {}

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
      UnexpectedEOF(/*min_progress_size=*/1);
      return absl::optional<uint8_t>();
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
    // We allow up to 16 just for kicks, but any more and we'll assume the
    // sender is being malicious.
    int num_redundant_0x80 = 0;
    do {
      cur = Next();
      if (!cur.has_value()) return {};
      ++num_redundant_0x80;
      if (num_redundant_0x80 == 16) {
        return ParseVarintMaliciousEncoding();
      }
    } while (*cur == 0x80);

    // BUT... the last byte needs to be 0x00 or we'll overflow dramatically!
    if (*cur == 0) return value;
    return ParseVarintOutOfRange(value, *cur);
  }

  // Parse a string prefix
  absl::optional<StringPrefix> ParseStringPrefix() {
    auto cur = Next();
    if (!cur.has_value()) {
      DCHECK(eof_error());
      return {};
    }
    // Huffman if the top bit is 1
    const bool huff = (*cur & 0x80) != 0;
    // String length
    uint32_t strlen = (*cur & 0x7f);
    if (strlen == 0x7f) {
      // all ones ==> varint string length
      auto v = ParseVarint(0x7f);
      if (!v.has_value()) {
        DCHECK(eof_error());
        return {};
      }
      strlen = *v;
    }
    return StringPrefix{strlen, huff};
  }

  // Check if we saw an EOF
  bool eof_error() const {
    return min_progress_size_ != 0 || frame_error_.connection_error();
  }

  // Reset the field error to be ok
  void ClearFieldError() {
    if (field_error_.ok()) return;
    field_error_ = HpackParseResult();
  }

  // Minimum number of bytes to unstuck the current parse
  size_t min_progress_size() const { return min_progress_size_; }

  // Set the current error - tweaks the error to include a stream id so that
  // chttp2 does not close the connection.
  // Intended for errors that are specific to a stream and recoverable.
  // Callers should ensure that any hpack table updates happen.
  void SetErrorAndContinueParsing(HpackParseResult error) {
    DCHECK(error.stream_error());
    SetError(std::move(error));
  }

  // Set the current error, and skip past remaining bytes.
  // Intended for unrecoverable errors, with the expectation that they will
  // close the connection on return to chttp2.
  void SetErrorAndStopParsing(HpackParseResult error) {
    DCHECK(error.connection_error());
    SetError(std::move(error));
    begin_ = end_;
  }

  // Set the error to an unexpected eof.
  // min_progress_size: how many bytes beyond the current frontier do we need to
  // read prior to being able to get further in this parse.
  void UnexpectedEOF(size_t min_progress_size) {
    CHECK_GT(min_progress_size, 0u);
    if (eof_error()) return;
    // Set min progress size, taking into account bytes parsed already but not
    // consumed.
    min_progress_size_ = min_progress_size + (begin_ - frontier_);
    DCHECK(eof_error());
  }

  // Update the frontier - signifies we've successfully parsed another element
  void UpdateFrontier() {
    DCHECK_EQ(skip_bytes_, 0u);
    frontier_ = begin_;
  }

  void UpdateFrontierAndSkipBytes(size_t skip_bytes) {
    UpdateFrontier();
    size_t remaining = end_ - begin_;
    if (skip_bytes >= remaining) {
      // If we have more bytes to skip than we have remaining in this buffer
      // then we skip over what's there and stash that we need to skip some
      // more.
      skip_bytes_ = skip_bytes - remaining;
      frontier_ = end_;
    } else {
      // Otherwise we zoom through some bytes and continue parsing.
      frontier_ += skip_bytes_;
    }
  }

  // Get the frontier - for buffering should we fail due to eof
  const uint8_t* frontier() const { return frontier_; }

  // Access the rng
  absl::BitGenRef bitsrc() { return bitsrc_; }

 private:
  // Helper to set the error to out of range for ParseVarint
  absl::optional<uint32_t> ParseVarintOutOfRange(uint32_t value,
                                                 uint8_t last_byte) {
    SetErrorAndStopParsing(
        HpackParseResult::VarintOutOfRangeError(value, last_byte));
    return absl::optional<uint32_t>();
  }

  // Helper to set the error in the case of a malicious encoding
  absl::optional<uint32_t> ParseVarintMaliciousEncoding() {
    SetErrorAndStopParsing(HpackParseResult::MaliciousVarintEncodingError());
    return absl::optional<uint32_t>();
  }

  // If no error is set, set it to the given error (i.e. first error wins)
  // Do not use this directly, instead use SetErrorAndContinueParsing or
  // SetErrorAndStopParsing.
  void SetError(HpackParseResult error) {
    SetErrorFor(frame_error_, error);
    SetErrorFor(field_error_, std::move(error));
  }

  void SetErrorFor(HpackParseResult& error, HpackParseResult new_error) {
    if (!error.ok() || min_progress_size_ > 0) {
      if (new_error.connection_error() && !error.connection_error()) {
        error = std::move(new_error);  // connection errors dominate
      }
      return;
    }
    error = std::move(new_error);
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
  HpackParseResult& frame_error_;
  HpackParseResult& field_error_;
  // If the error was EOF, we flag it here by noting how many more bytes would
  // be needed to make progress
  size_t min_progress_size_ = 0;
  // Number of bytes that should be skipped before parsing resumes.
  // (We've failed parsing a request for whatever reason, but we're still
  // continuing the connection so we need to see future opcodes after this bit).
  size_t skip_bytes_ = 0;
  // Random number generator
  absl::BitGenRef bitsrc_;
};

absl::string_view HPackParser::String::string_view() const {
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

template <typename Out>
HpackParseStatus HPackParser::String::ParseHuff(Input* input, uint32_t length,
                                                Out output) {
  // If there's insufficient bytes remaining, return now.
  if (input->remaining() < length) {
    input->UnexpectedEOF(/*min_progress_size=*/length);
    return HpackParseStatus::kEof;
  }
  // Grab the byte range, and iterate through it.
  const uint8_t* p = input->cur_ptr();
  input->Advance(length);
  return HuffDecoder<Out>(output, p, p + length).Run()
             ? HpackParseStatus::kOk
             : HpackParseStatus::kParseHuffFailed;
}

struct HPackParser::String::StringResult {
  StringResult() = delete;
  StringResult(HpackParseStatus status, size_t wire_size, String value)
      : status(status), wire_size(wire_size), value(std::move(value)) {}
  HpackParseStatus status;
  size_t wire_size;
  String value;
};

HPackParser::String::StringResult HPackParser::String::ParseUncompressed(
    Input* input, uint32_t length, uint32_t wire_size) {
  // Check there's enough bytes
  if (input->remaining() < length) {
    input->UnexpectedEOF(/*min_progress_size=*/length);
    DCHECK(input->eof_error());
    return StringResult{HpackParseStatus::kEof, wire_size, String{}};
  }
  auto* refcount = input->slice_refcount();
  auto* p = input->cur_ptr();
  input->Advance(length);
  if (refcount != nullptr) {
    return StringResult{HpackParseStatus::kOk, wire_size,
                        String(refcount, p, p + length)};
  } else {
    return StringResult{HpackParseStatus::kOk, wire_size,
                        String(absl::Span<const uint8_t>(p, length))};
  }
}

absl::optional<std::vector<uint8_t>> HPackParser::String::Unbase64Loop(
    const uint8_t* cur, const uint8_t* end) {
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

HPackParser::String::StringResult HPackParser::String::Unbase64(String s) {
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
    return StringResult{HpackParseStatus::kUnbase64Failed,
                        s.string_view().length(), String{}};
  }
  return StringResult{HpackParseStatus::kOk, s.string_view().length(),
                      String(std::move(*result))};
}

HPackParser::String::StringResult HPackParser::String::Parse(Input* input,
                                                             bool is_huff,
                                                             size_t length) {
  if (is_huff) {
    // Huffman coded
    std::vector<uint8_t> output;
    HpackParseStatus sts =
        ParseHuff(input, length, [&output](uint8_t c) { output.push_back(c); });
    size_t wire_len = output.size();
    return StringResult{sts, wire_len, String(std::move(output))};
  }
  return ParseUncompressed(input, length, length);
}

HPackParser::String::StringResult HPackParser::String::ParseBinary(
    Input* input, bool is_huff, size_t length) {
  if (!is_huff) {
    if (length > 0 && input->peek() == 0) {
      // 'true-binary'
      input->Advance(1);
      return ParseUncompressed(input, length - 1, length);
    }
    // Base64 encoded... pull out the string, then unbase64 it
    auto base64 = ParseUncompressed(input, length, length);
    if (base64.status != HpackParseStatus::kOk) return base64;
    return Unbase64(std::move(base64.value));
  } else {
    // Huffman encoded...
    std::vector<uint8_t> decompressed;
    // State here says either we don't know if it's base64 or binary, or we do
    // and what is it.
    enum class State { kUnsure, kBinary, kBase64 };
    State state = State::kUnsure;
    auto sts = ParseHuff(input, length, [&state, &decompressed](uint8_t c) {
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
    if (sts != HpackParseStatus::kOk) {
      return StringResult{sts, 0, String{}};
    }
    switch (state) {
      case State::kUnsure:
        // No bytes, empty span
        return StringResult{HpackParseStatus::kOk, 0,
                            String(absl::Span<const uint8_t>())};
      case State::kBinary:
        // Binary, we're done
        {
          size_t wire_len = decompressed.size();
          return StringResult{HpackParseStatus::kOk, wire_len,
                              String(std::move(decompressed))};
        }
      case State::kBase64:
        // Base64 - unpack it
        return Unbase64(String(std::move(decompressed)));
    }
    GPR_UNREACHABLE_CODE(abort(););
  }
}

// Parser parses one key/value pair from a byte stream.
class HPackParser::Parser {
 public:
  Parser(Input* input, grpc_metadata_batch*& metadata_buffer,
         InterSliceState& state, LogInfo log_info)
      : input_(input),
        metadata_buffer_(metadata_buffer),
        state_(state),
        log_info_(log_info) {}

  bool Parse() {
    switch (state_.parse_state) {
      case ParseState::kTop:
        return ParseTop();
      case ParseState::kParsingKeyLength:
        return ParseKeyLength();
      case ParseState::kParsingKeyBody:
        return ParseKeyBody();
      case ParseState::kSkippingKeyBody:
        return SkipKeyBody();
      case ParseState::kParsingValueLength:
        return ParseValueLength();
      case ParseState::kParsingValueBody:
        return ParseValueBody();
      case ParseState::kSkippingValueLength:
        return SkipValueLength();
      case ParseState::kSkippingValueBody:
        return SkipValueBody();
    }
    GPR_UNREACHABLE_CODE(return false);
  }

 private:
  bool ParseTop() {
    DCHECK(state_.parse_state == ParseState::kTop);
    auto cur = *input_->Next();
    input_->ClearFieldError();
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
            return StartParseLiteralKey(false);
          case 0xf:  // varint encoded key index
            return StartVarIdxKey(0xf, false);
          default:  // inline encoded key index
            return StartIdxKey(cur & 0xf, false);
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
          return StartParseLiteralKey(true);
        }
        ABSL_FALLTHROUGH_INTENDED;
      case 5:
      case 6:
        // inline encoded key index
        return StartIdxKey(cur & 0x3f, true);
      case 7:
        if (cur == 0x7f) {
          // varint encoded key index
          return StartVarIdxKey(0x3f, true);
        } else {
          // inline encoded key index
          return StartIdxKey(cur & 0x3f, true);
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
          input_->SetErrorAndStopParsing(
              HpackParseResult::IllegalHpackOpCode());
          return false;
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
    gpr_log(
        GPR_INFO, "HTTP:%d:%s:%s: %s%s", log_info_.stream_id, type,
        log_info_.is_client ? "CLI" : "SVR", memento.md.DebugString().c_str(),
        memento.parse_status == nullptr
            ? ""
            : absl::StrCat(" (parse error: ",
                           memento.parse_status->Materialize().ToString(), ")")
                  .c_str());
  }

  void EmitHeader(const HPackTable::Memento& md) {
    // Pass up to the transport
    state_.frame_length += md.md.transport_size();
    if (md.parse_status != nullptr) {
      // Reject any requests with invalid metadata.
      input_->SetErrorAndContinueParsing(*md.parse_status);
    }
    if (GPR_LIKELY(metadata_buffer_ != nullptr)) {
      metadata_buffer_->Set(md.md);
    }
    if (state_.metadata_early_detection.MustReject(state_.frame_length)) {
      // Reject any requests above hard metadata limit.
      input_->SetErrorAndContinueParsing(
          HpackParseResult::HardMetadataLimitExceededError(
              std::exchange(metadata_buffer_, nullptr), state_.frame_length,
              state_.metadata_early_detection.hard_limit()));
    }
  }

  bool FinishHeaderAndAddToTable(HPackTable::Memento md) {
    // Log if desired
    if (GRPC_TRACE_FLAG_ENABLED(chttp2_hpack_parser)) {
      LogHeader(md);
    }
    // Emit whilst we own the metadata.
    EmitHeader(md);
    // Add to the hpack table
    if (GPR_UNLIKELY(!state_.hpack_table.Add(std::move(md)))) {
      input_->SetErrorAndStopParsing(
          HpackParseResult::AddBeforeTableSizeUpdated(
              state_.hpack_table.current_table_bytes(),
              state_.hpack_table.max_bytes()));
      return false;
    };
    return true;
  }

  bool FinishHeaderOmitFromTable(absl::optional<HPackTable::Memento> md) {
    // Allow higher code to just pass in failures ... simplifies things a bit.
    if (!md.has_value()) return false;
    FinishHeaderOmitFromTable(*md);
    return true;
  }

  void FinishHeaderOmitFromTable(const HPackTable::Memento& md) {
    // Log if desired
    if (GRPC_TRACE_FLAG_ENABLED(chttp2_hpack_parser)) {
      LogHeader(md);
    }
    EmitHeader(md);
  }

  // Parse an index encoded key and a string encoded value
  bool StartIdxKey(uint32_t index, bool add_to_table) {
    DCHECK(state_.parse_state == ParseState::kTop);
    input_->UpdateFrontier();
    const auto* elem = state_.hpack_table.Lookup(index);
    if (GPR_UNLIKELY(elem == nullptr)) {
      InvalidHPackIndexError(index);
      return false;
    }
    state_.parse_state = ParseState::kParsingValueLength;
    state_.is_binary_header = elem->md.is_binary_header();
    state_.key.emplace<const HPackTable::Memento*>(elem);
    state_.add_to_table = add_to_table;
    return ParseValueLength();
  };

  // Parse a varint index encoded key and a string encoded value
  bool StartVarIdxKey(uint32_t offset, bool add_to_table) {
    DCHECK(state_.parse_state == ParseState::kTop);
    auto index = input_->ParseVarint(offset);
    if (GPR_UNLIKELY(!index.has_value())) return false;
    return StartIdxKey(*index, add_to_table);
  }

  bool StartParseLiteralKey(bool add_to_table) {
    DCHECK(state_.parse_state == ParseState::kTop);
    state_.add_to_table = add_to_table;
    state_.parse_state = ParseState::kParsingKeyLength;
    input_->UpdateFrontier();
    return ParseKeyLength();
  }

  bool ShouldSkipParsingString(uint64_t string_length) const {
    // We skip parsing if the string is longer than the current table size, and
    // if we would have to reject the string due to metadata length limits
    // regardless of what else was in the metadata batch.
    //
    // Why longer than the current table size? - it simplifies the logic at the
    // end of skipping the string (and possibly a second if this is a key).
    // If a key/value pair longer than the current table size is added to the
    // hpack table we're forced to clear the entire table - this is a
    // predictable operation that's easy to encode and doesn't need any state
    // other than "skipping" to be carried forward.
    // If we did not do this, we could end up in a situation where even though
    // the metadata would overflow the current limit, it might not overflow the
    // current hpack table size, and so we could not skip in on the off chance
    // that we'd need to add it to the hpack table *and* reject the batch as a
    // whole.
    // That would be a mess, we're not doing it.
    //
    // These rules will end up having us parse some things that ultimately get
    // rejected, and that's ok: the important thing is to have a bounded maximum
    // so we can't be forced to infinitely buffer - not to have a perfect
    // computation here.
    return string_length > state_.hpack_table.current_table_size() &&
           state_.metadata_early_detection.MustReject(
               string_length + hpack_constants::kEntryOverhead);
  }

  bool ParseKeyLength() {
    DCHECK(state_.parse_state == ParseState::kParsingKeyLength);
    auto pfx = input_->ParseStringPrefix();
    if (!pfx.has_value()) return false;
    state_.is_string_huff_compressed = pfx->huff;
    state_.string_length = pfx->length;
    input_->UpdateFrontier();
    if (ShouldSkipParsingString(state_.string_length)) {
      input_->SetErrorAndContinueParsing(
          HpackParseResult::HardMetadataLimitExceededByKeyError(
              state_.string_length,
              state_.metadata_early_detection.hard_limit()));
      metadata_buffer_ = nullptr;
      state_.parse_state = ParseState::kSkippingKeyBody;
      return SkipKeyBody();
    } else {
      state_.parse_state = ParseState::kParsingKeyBody;
      return ParseKeyBody();
    }
  }

  bool ParseKeyBody() {
    DCHECK(state_.parse_state == ParseState::kParsingKeyBody);
    auto key = String::Parse(input_, state_.is_string_huff_compressed,
                             state_.string_length);
    switch (key.status) {
      case HpackParseStatus::kOk:
        break;
      case HpackParseStatus::kEof:
        DCHECK(input_->eof_error());
        return false;
      default:
        input_->SetErrorAndStopParsing(
            HpackParseResult::FromStatus(key.status));
        return false;
    }
    input_->UpdateFrontier();
    state_.parse_state = ParseState::kParsingValueLength;
    state_.is_binary_header = absl::EndsWith(key.value.string_view(), "-bin");
    state_.key.emplace<Slice>(key.value.Take());
    return ParseValueLength();
  }

  bool SkipStringBody() {
    auto remaining = input_->remaining();
    if (remaining >= state_.string_length) {
      input_->Advance(state_.string_length);
      return true;
    } else {
      input_->Advance(remaining);
      input_->UpdateFrontier();
      state_.string_length -= remaining;
      // The default action of our outer loop is to buffer up to
      // min_progress_size bytes.
      // We know we need to do nothing up to the string length, so it would be
      // legal to pass that here - however that would cause a client selected
      // large buffer size to be accumulated, which would be an attack vector.
      // We could also pass 1 here, and we'd be called to parse potentially
      // every byte, which would give clients a way to consume substantial CPU -
      // again not great.
      // So we pick some tradeoff number - big enough to amortize wakeups, but
      // probably not big enough to cause excessive memory use on the receiver.
      input_->UnexpectedEOF(
          /*min_progress_size=*/std::min(state_.string_length, 1024u));
      return false;
    }
  }

  bool SkipKeyBody() {
    DCHECK(state_.parse_state == ParseState::kSkippingKeyBody);
    if (!SkipStringBody()) return false;
    input_->UpdateFrontier();
    state_.parse_state = ParseState::kSkippingValueLength;
    return SkipValueLength();
  }

  bool SkipValueLength() {
    DCHECK(state_.parse_state == ParseState::kSkippingValueLength);
    auto pfx = input_->ParseStringPrefix();
    if (!pfx.has_value()) return false;
    state_.string_length = pfx->length;
    input_->UpdateFrontier();
    state_.parse_state = ParseState::kSkippingValueBody;
    return SkipValueBody();
  }

  bool SkipValueBody() {
    DCHECK(state_.parse_state == ParseState::kSkippingValueBody);
    if (!SkipStringBody()) return false;
    input_->UpdateFrontier();
    state_.parse_state = ParseState::kTop;
    if (state_.add_to_table) {
      state_.hpack_table.AddLargerThanCurrentTableSize();
    }
    return true;
  }

  bool ParseValueLength() {
    DCHECK(state_.parse_state == ParseState::kParsingValueLength);
    auto pfx = input_->ParseStringPrefix();
    if (!pfx.has_value()) return false;
    state_.is_string_huff_compressed = pfx->huff;
    state_.string_length = pfx->length;
    input_->UpdateFrontier();
    if (ShouldSkipParsingString(state_.string_length)) {
      input_->SetErrorAndContinueParsing(
          HpackParseResult::HardMetadataLimitExceededByValueError(
              Match(
                  state_.key, [](const Slice& s) { return s.as_string_view(); },
                  [](const HPackTable::Memento* m) { return m->md.key(); }),
              state_.string_length,
              state_.metadata_early_detection.hard_limit()));
      metadata_buffer_ = nullptr;
      state_.parse_state = ParseState::kSkippingValueBody;
      return SkipValueBody();
    } else {
      state_.parse_state = ParseState::kParsingValueBody;
      return ParseValueBody();
    }
  }

  bool ParseValueBody() {
    DCHECK(state_.parse_state == ParseState::kParsingValueBody);
    auto value =
        state_.is_binary_header
            ? String::ParseBinary(input_, state_.is_string_huff_compressed,
                                  state_.string_length)
            : String::Parse(input_, state_.is_string_huff_compressed,
                            state_.string_length);
    absl::string_view key_string;
    if (auto* s = absl::get_if<Slice>(&state_.key)) {
      key_string = s->as_string_view();
      if (state_.field_error.ok()) {
        auto r = ValidateKey(key_string);
        if (r != ValidateMetadataResult::kOk) {
          input_->SetErrorAndContinueParsing(
              HpackParseResult::InvalidMetadataError(r, key_string));
        }
      }
    } else {
      const auto* memento = absl::get<const HPackTable::Memento*>(state_.key);
      key_string = memento->md.key();
      if (state_.field_error.ok() && memento->parse_status != nullptr) {
        input_->SetErrorAndContinueParsing(*memento->parse_status);
      }
    }
    switch (value.status) {
      case HpackParseStatus::kOk:
        break;
      case HpackParseStatus::kEof:
        DCHECK(input_->eof_error());
        return false;
      default: {
        auto result =
            HpackParseResult::FromStatusWithKey(value.status, key_string);
        if (result.stream_error()) {
          input_->SetErrorAndContinueParsing(std::move(result));
          break;
        } else {
          input_->SetErrorAndStopParsing(std::move(result));
          return false;
        }
      }
    }
    auto value_slice = value.value.Take();
    const auto transport_size =
        key_string.size() + value.wire_size + hpack_constants::kEntryOverhead;
    auto md = grpc_metadata_batch::Parse(
        key_string, std::move(value_slice), state_.add_to_table, transport_size,
        [key_string, this](absl::string_view message, const Slice&) {
          if (!state_.field_error.ok()) return;
          input_->SetErrorAndContinueParsing(
              HpackParseResult::MetadataParseError(key_string));
          gpr_log(GPR_ERROR, "Error parsing '%s' metadata: %s",
                  std::string(key_string).c_str(),
                  std::string(message).c_str());
        });
    HPackTable::Memento memento{
        std::move(md), state_.field_error.PersistentStreamErrorOrNullptr()};
    input_->UpdateFrontier();
    state_.parse_state = ParseState::kTop;
    if (state_.add_to_table) {
      return FinishHeaderAndAddToTable(std::move(memento));
    } else {
      FinishHeaderOmitFromTable(memento);
      return true;
    }
  }

  ValidateMetadataResult ValidateKey(absl::string_view key) {
    if (key == HttpSchemeMetadata::key() || key == HttpMethodMetadata::key() ||
        key == HttpAuthorityMetadata::key() || key == HttpPathMetadata::key() ||
        key == HttpStatusMetadata::key()) {
      return ValidateMetadataResult::kOk;
    }
    return ValidateHeaderKeyIsLegal(key);
  }

  // Emit an indexed field
  bool FinishIndexed(absl::optional<uint32_t> index) {
    state_.dynamic_table_updates_allowed = 0;
    if (!index.has_value()) return false;
    const auto* elem = state_.hpack_table.Lookup(*index);
    if (GPR_UNLIKELY(elem == nullptr)) {
      InvalidHPackIndexError(*index);
      return false;
    }
    FinishHeaderOmitFromTable(*elem);
    return true;
  }

  // finish parsing a max table size change
  bool FinishMaxTableSize(absl::optional<uint32_t> size) {
    if (!size.has_value()) return false;
    if (state_.dynamic_table_updates_allowed == 0) {
      input_->SetErrorAndStopParsing(
          HpackParseResult::TooManyDynamicTableSizeChangesError());
      return false;
    }
    state_.dynamic_table_updates_allowed--;
    if (!state_.hpack_table.SetCurrentTableSize(*size)) {
      input_->SetErrorAndStopParsing(
          HpackParseResult::IllegalTableSizeChangeError(
              *size, state_.hpack_table.max_bytes()));
      return false;
    }
    return true;
  }

  // Set an invalid hpack index error if no error has been set. Returns result
  // unmodified.
  void InvalidHPackIndexError(uint32_t index) {
    input_->SetErrorAndStopParsing(
        HpackParseResult::InvalidHpackIndexError(index));
  }

  Input* const input_;
  grpc_metadata_batch*& metadata_buffer_;
  InterSliceState& state_;
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
                             uint32_t metadata_size_soft_limit,
                             uint32_t metadata_size_hard_limit,
                             Boundary boundary, Priority priority,
                             LogInfo log_info) {
  metadata_buffer_ = metadata_buffer;
  if (metadata_buffer != nullptr) {
    metadata_buffer->Set(GrpcStatusFromWire(), true);
  }
  boundary_ = boundary;
  priority_ = priority;
  state_.dynamic_table_updates_allowed = 2;
  state_.metadata_early_detection.SetLimits(
      /*soft_limit=*/metadata_size_soft_limit,
      /*hard_limit=*/metadata_size_hard_limit);
  log_info_ = log_info;
}

grpc_error_handle HPackParser::Parse(
    const grpc_slice& slice, bool is_last, absl::BitGenRef bitsrc,
    CallTracerAnnotationInterface* call_tracer) {
  if (GPR_UNLIKELY(!unparsed_bytes_.empty())) {
    unparsed_bytes_.insert(unparsed_bytes_.end(), GRPC_SLICE_START_PTR(slice),
                           GRPC_SLICE_END_PTR(slice));
    if (!(is_last && is_boundary()) &&
        unparsed_bytes_.size() < min_progress_size_) {
      // We wouldn't make progress anyway, skip out.
      return absl::OkStatus();
    }
    std::vector<uint8_t> buffer = std::move(unparsed_bytes_);
    return ParseInput(
        Input(nullptr, buffer.data(), buffer.data() + buffer.size(), bitsrc,
              state_.frame_error, state_.field_error),
        is_last, call_tracer);
  }
  return ParseInput(Input(slice.refcount, GRPC_SLICE_START_PTR(slice),
                          GRPC_SLICE_END_PTR(slice), bitsrc, state_.frame_error,
                          state_.field_error),
                    is_last, call_tracer);
}

grpc_error_handle HPackParser::ParseInput(
    Input input, bool is_last, CallTracerAnnotationInterface* call_tracer) {
  ParseInputInner(&input);
  if (is_last && is_boundary()) {
    if (state_.metadata_early_detection.Reject(state_.frame_length,
                                               input.bitsrc())) {
      HandleMetadataSoftSizeLimitExceeded(&input);
    }
    global_stats().IncrementHttp2MetadataSize(state_.frame_length);
    if (call_tracer != nullptr && metadata_buffer_ != nullptr) {
      MetadataSizesAnnotation metadata_sizes_annotation(
          metadata_buffer_, state_.metadata_early_detection.soft_limit(),
          state_.metadata_early_detection.hard_limit());
      call_tracer->RecordAnnotation(metadata_sizes_annotation);
    }
    if (!state_.frame_error.connection_error() &&
        (input.eof_error() || state_.parse_state != ParseState::kTop)) {
      state_.frame_error = HpackParseResult::IncompleteHeaderAtBoundaryError();
    }
    state_.frame_length = 0;
    return std::exchange(state_.frame_error, HpackParseResult()).Materialize();
  } else {
    if (input.eof_error() && !state_.frame_error.connection_error()) {
      unparsed_bytes_ = std::vector<uint8_t>(input.frontier(), input.end_ptr());
      min_progress_size_ = input.min_progress_size();
    }
    return state_.frame_error.Materialize();
  }
}

void HPackParser::ParseInputInner(Input* input) {
  switch (priority_) {
    case Priority::None:
      break;
    case Priority::Included: {
      if (input->remaining() < 5) {
        input->UnexpectedEOF(/*min_progress_size=*/5);
        return;
      }
      input->Advance(5);
      input->UpdateFrontier();
      priority_ = Priority::None;
    }
  }
  while (!input->end_of_stream()) {
    if (GPR_UNLIKELY(
            !Parser(input, metadata_buffer_, state_, log_info_).Parse())) {
      return;
    }
    input->UpdateFrontier();
  }
}

void HPackParser::FinishFrame() { metadata_buffer_ = nullptr; }

void HPackParser::HandleMetadataSoftSizeLimitExceeded(Input* input) {
  input->SetErrorAndContinueParsing(
      HpackParseResult::SoftMetadataLimitExceededError(
          std::exchange(metadata_buffer_, nullptr), state_.frame_length,
          state_.metadata_early_detection.soft_limit()));
}

}  // namespace grpc_core
