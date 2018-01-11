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

#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/http2_errors.h"

/* How parsing works:

   The parser object keeps track of a function pointer which represents the
   current parse state.

   Each time new bytes are presented, we call into the current state, which
   recursively parses until all bytes in the given chunk are exhausted.

   The parse state that terminates then saves its function pointer to be the
   current state so that it can resume when more bytes are available.

   It's expected that most optimizing compilers will turn this code into
   a set of indirect jumps, and so not waste stack space. */

namespace grpc_core {
namespace chttp2 {

class HpackParser::ParseContext {
 public:
  ParseContext(HpackParser* parser,
               grpc_core::metadata::Collection* parsing_metadata,
               grpc_slice_refcount* current_slice_refcount,
               const uint8_t* start, const uint8_t* end);

  /* initial parsing state: public so that the parser can access it */
  grpc_error* ParseBegin() GRPC_MUST_USE_RESULT;

 private:
  /* all the other parsing states */
  grpc_error* StillParseError() GRPC_MUST_USE_RESULT;
  grpc_error* ParseIllegalOp() GRPC_MUST_USE_RESULT;
  grpc_error* ParseStringPrefix() GRPC_MUST_USE_RESULT;
  grpc_error* ParseStreamWeight() GRPC_MUST_USE_RESULT;
  grpc_error* ParseDep0() GRPC_MUST_USE_RESULT;
  grpc_error* ParseDep1() GRPC_MUST_USE_RESULT;
  grpc_error* ParseDep2() GRPC_MUST_USE_RESULT;
  grpc_error* ParseDep3() GRPC_MUST_USE_RESULT;
  grpc_error* ParseKeyString() GRPC_MUST_USE_RESULT;
  grpc_error* ParseValueStringWithIndexedKey() GRPC_MUST_USE_RESULT;
  grpc_error* ParseValueStringWithLiteralKey() GRPC_MUST_USE_RESULT;
  grpc_error* ParseValue0() GRPC_MUST_USE_RESULT;
  grpc_error* ParseValue1() GRPC_MUST_USE_RESULT;
  grpc_error* ParseValue2() GRPC_MUST_USE_RESULT;
  grpc_error* ParseValue3() GRPC_MUST_USE_RESULT;
  grpc_error* ParseValue4() GRPC_MUST_USE_RESULT;
  grpc_error* ParseValue5Up() GRPC_MUST_USE_RESULT;
  grpc_error* Parse_Indexed() GRPC_MUST_USE_RESULT;
  grpc_error* Parse_Indexed_X() GRPC_MUST_USE_RESULT;
  grpc_error* Parse_LitHdr_IncIdx() GRPC_MUST_USE_RESULT;
  grpc_error* Parse_LitHdr_IncIdx_X() GRPC_MUST_USE_RESULT;
  grpc_error* Parse_LitHdr_IncIdx_V() GRPC_MUST_USE_RESULT;
  grpc_error* Parse_LitHdr_NotIdx() GRPC_MUST_USE_RESULT;
  grpc_error* Parse_LitHdr_NotIdx_X() GRPC_MUST_USE_RESULT;
  grpc_error* Parse_LitHdr_NotIdx_V() GRPC_MUST_USE_RESULT;
  grpc_error* Parse_LitHdr_NvrIdx() GRPC_MUST_USE_RESULT;
  grpc_error* Parse_LitHdr_NvrIdx_X() GRPC_MUST_USE_RESULT;
  grpc_error* Parse_LitHdr_NvrIdx_V() GRPC_MUST_USE_RESULT;
  grpc_error* ParseMaxTableSize() GRPC_MUST_USE_RESULT;
  grpc_error* ParseMaxTableSize_X() GRPC_MUST_USE_RESULT;
  grpc_error* ParseString() GRPC_MUST_USE_RESULT;

  /* common tails for parsing states */
  grpc_error* ParseError(grpc_error* error) const GRPC_MUST_USE_RESULT;
  grpc_error* FinishIndexedField() GRPC_MUST_USE_RESULT;
  grpc_error* Finish_LitHdr_IncIdx_Tail(const metadata::Key* key)
      GRPC_MUST_USE_RESULT;
  grpc_error* Finish_LitHdr_IncIdx() GRPC_MUST_USE_RESULT;
  grpc_error* Finish_LitHdr_IncIdx_V() GRPC_MUST_USE_RESULT;
  grpc_error* Finish_LitHdr_NotIdx() GRPC_MUST_USE_RESULT;
  grpc_error* Finish_LitHdr_NotIdx_V() GRPC_MUST_USE_RESULT;
  grpc_error* Finish_LitHdr_NvrIdx() GRPC_MUST_USE_RESULT;
  grpc_error* Finish_LitHdr_NvrIdx_V() GRPC_MUST_USE_RESULT;
  grpc_error* FinishMaxTblSize() GRPC_MUST_USE_RESULT;
  grpc_error* IndexOutOfRangeError() GRPC_MUST_USE_RESULT;
  grpc_error* IntegerOverflowError(const char* where) GRPC_MUST_USE_RESULT;
  grpc_error* TooManyTableSizeChanges() GRPC_MUST_USE_RESULT;
  grpc_error* FinishInState(State state) GRPC_MUST_USE_RESULT;
  grpc_error* FinishWithErrorInState(grpc_error* error,
                                     State save_state) GRPC_MUST_USE_RESULT;
  grpc_error* BeginParseString(BinaryParseState binary,
                               ParserString* str) GRPC_MUST_USE_RESULT;

  // String decoding routines - const against ParseContext, but may mutate
  // parser
  grpc_error* AppendString(const uint8_t* beg,
                           const uint8_t* end) const GRPC_MUST_USE_RESULT;
  grpc_error* FinishStr() GRPC_MUST_USE_RESULT;
  grpc_error* HuffNibble(uint8_t nibble) const GRPC_MUST_USE_RESULT;
  grpc_error* AddHuffBytes(const uint8_t* beg,
                           const uint8_t* end) const GRPC_MUST_USE_RESULT;
  grpc_error* AddStrBytes(const uint8_t* beg,
                          const uint8_t* end) const GRPC_MUST_USE_RESULT;

  bool IsEndOfSlice() const { return cur_ == end_; }

  template <int kBytesToConsume, State kNextState>
  grpc_error* ConsumeAndCall();
  template <int kBytesToConsume>
  grpc_error* ConsumeAndCall(State next_state);
  grpc_error* ConsumeAndCallNext(int bytes_to_consume);
  template <int kBytesToConsume, State kNextStateIfTrue>
  grpc_error* ConsumeAndCallIfOrNext(bool cond);
  template <int kBytesToConsume, State kNextState>
  grpc_error* ParseErrorOrConsumeAndCall(grpc_error* error);

  static metadata::Key* KeyFromName(grpc_slice name);

  HpackParser* const parser_;
  grpc_core::metadata::Collection* const parsing_metadata_;
  /* the refcount of the slice that we're currently parsing */
  grpc_slice_refcount* const current_slice_refcount_;
  const uint8_t* cur_;
  const uint8_t* const end_;

  /* we translate the first byte of a hpack field into one of these decoding
     cases, then use a lookup table to jump directly to the appropriate parser.

     _X => the integer index is all ones, meaning we need to do varint decoding
     _V => the integer index is all zeros, meaning we need to decode an
     additional string value */
  enum class FirstByteType : uint8_t {
    INDEXED_FIELD,
    INDEXED_FIELD_X,
    LITHDR_INCIDX,
    LITHDR_INCIDX_X,
    LITHDR_INCIDX_V,
    LITHDR_NOTIDX,
    LITHDR_NOTIDX_X,
    LITHDR_NOTIDX_V,
    LITHDR_NVRIDX,
    LITHDR_NVRIDX_X,
    LITHDR_NVRIDX_V,
    MAX_TBL_SIZE,
    MAX_TBL_SIZE_X,
    ILLEGAL
  };

  static const State first_byte_action_[];
  static const FirstByteType first_byte_lut_[256];
  static const uint8_t next_tbl_[256];
  static const int16_t next_sub_tbl_[48 * 16];
  static const uint16_t emit_tbl_[256];
  static const int16_t emit_sub_tbl_[249 * 16];
  static const uint8_t inverse_base64_[256];
};  // namespace grpc_core

grpc_error* HpackParser::ParseSlice(
    grpc_slice slice, grpc_core::metadata::Collection* parsing_metadata) {
  /* max number of bytes to parse at a time... limits call stack depth on
   * compilers without TCO */
  constexpr size_t kMaxParseLength = 1024;
  uint8_t* start = GRPC_SLICE_START_PTR(slice);
  uint8_t* end = GRPC_SLICE_END_PTR(slice);
  grpc_error* error = GRPC_ERROR_NONE;
  while (start != end && error == GRPC_ERROR_NONE) {
    uint8_t* target = start + GPR_MIN(kMaxParseLength, end - start);
    ParseContext ctx(this, parsing_metadata, slice.refcount, start, target);
    error = (ctx.*state_)();
    start = target;
  }
  return error;
}

/* a parse error: jam the parse state into parse_error, and return error */
grpc_error* HpackParser::ParseContext::ParseError(grpc_error* error) const {
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  if (parser_->last_error_ == GRPC_ERROR_NONE) {
    parser_->last_error_ = GRPC_ERROR_REF(error);
  }
  parser_->state_ = &ParseContext::StillParseError;
  return error;
}

grpc_error* HpackParser::ParseContext::StillParseError() {
  return GRPC_ERROR_REF(parser_->last_error_);
}

template <int kBytesToConsume, HpackParser::State kNextState>
grpc_error* HpackParser::ParseContext::ConsumeAndCall() {
  assert(cur_ + kBytesToConsume <= end_);
  cur_ += kBytesToConsume;
  return (this->*kNextState)();
}

template <int kBytesToConsume>
grpc_error* HpackParser::ParseContext::ConsumeAndCall(State next_state) {
  assert(cur_ + kBytesToConsume <= end_);
  cur_ += kBytesToConsume;
  return (this->*next_state)();
}

/* jump to the next state */
grpc_error* HpackParser::ParseContext::ConsumeAndCallNext(
    int bytes_to_consume) {
  cur_ += bytes_to_consume;
  State exec = *parser_->next_state_++;
  return (this->*exec)();
}

template <int kBytesToConsume, HpackParser::State kNextStateIfTrue>
grpc_error* HpackParser::ParseContext::ConsumeAndCallIfOrNext(bool cond) {
  assert(cur_ + kBytesToConsume <= end_);
  cur_ += kBytesToConsume;
  State exec = cond ? kNextStateIfTrue : (*parser_->next_state_++);
  return (this->*exec)();
}

template <int kBytesToConsume, HpackParser::State kNextState>
grpc_error* HpackParser::ParseContext::ParseErrorOrConsumeAndCall(
    grpc_error* error) {
  if (error != GRPC_ERROR_NONE) return ParseError(error);
  return ConsumeAndCall<kBytesToConsume, kNextState>();
}

/* begin parsing a header: all functionality is encoded into lookup tables
   above */
grpc_error* HpackParser::ParseContext::ParseBegin() {
  if (IsEndOfSlice()) return FinishInState(&ParseContext::ParseBegin);
  FirstByteType action_type = first_byte_lut_[*cur_];
  State action = first_byte_action_[static_cast<uint8_t>(action_type)];
  return (this->*action)();
}

/* stream dependency and prioritization data: we just skip it */
grpc_error* HpackParser::ParseContext::ParseStreamWeight() {
  if (IsEndOfSlice()) return FinishInState(&ParseContext::ParseStreamWeight);
  return ConsumeAndCall<1>(parser_->after_prioritization_);
}

grpc_error* HpackParser::ParseContext::ParseDep3() {
  if (IsEndOfSlice()) return FinishInState(&ParseContext::ParseDep3);
  return ConsumeAndCall<1, &ParseContext::ParseStreamWeight>();
}

grpc_error* HpackParser::ParseContext::ParseDep2() {
  if (IsEndOfSlice()) return FinishInState(&ParseContext::ParseDep2);
  return ConsumeAndCall<1, &ParseContext::ParseDep3>();
}

grpc_error* HpackParser::ParseContext::ParseDep1() {
  if (IsEndOfSlice()) return FinishInState(&ParseContext::ParseDep1);
  return ConsumeAndCall<1, &ParseContext::ParseDep2>();
}

grpc_error* HpackParser::ParseContext::ParseDep0() {
  if (IsEndOfSlice()) return FinishInState(&ParseContext::ParseDep0);
  return ConsumeAndCall<1, &ParseContext::ParseDep1>();
}

grpc_error* HpackParser::ParseContext::IndexOutOfRangeError() {
  return ParseError(grpc_error_set_int(
      grpc_error_set_int(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Invalid HPACK index received"),
          GRPC_ERROR_INT_INDEX, (intptr_t)parser_->index_),
      GRPC_ERROR_INT_SIZE, (intptr_t)parser_->table_.DynamicIndexCount()));
}

/* emit an indexed field; jumps to begin the next field on completion */
grpc_error* HpackParser::ParseContext::FinishIndexedField() {
  const HpackTable::Row* row = parser_->table_.Lookup(parser_->index_);
  if (row == nullptr) {
    return IndexOutOfRangeError();
  }
  GRPC_STATS_INC_HPACK_RECV_INDEXED();
  return ParseErrorOrConsumeAndCall<0, &ParseContext::ParseBegin>(
      row->key->SetInCollection(row->value, parsing_metadata_));
}

/* parse an indexed field with index < 127 */
grpc_error* HpackParser::ParseContext::Parse_Indexed() {
  parser_->dynamic_table_update_allowed_ = 0;
  parser_->index_ = (*cur_) & 0x7f;
  return FinishIndexedField();
}

/* parse an indexed field with index >= 127 */
grpc_error* HpackParser::ParseContext::Parse_Indexed_X() {
  static const State and_then[] = {&ParseContext::FinishIndexedField};
  parser_->dynamic_table_update_allowed_ = 0;
  parser_->next_state_ = and_then;
  parser_->index_ = 0x7f;
  parser_->parsing_.value = &parser_->index_;
  return ConsumeAndCall<1, &ParseContext::ParseValue0>();
}

grpc_error* HpackParser::ParseContext::Finish_LitHdr_IncIdx_Tail(
    const metadata::Key* key) {
  metadata::AnyValue value;
  grpc_error* error = key->Parse(parser_->value_.Take(true), &value);
  if (error != GRPC_ERROR_NONE) return ParseError(error);
  error = key->SetInCollection(value, parsing_metadata_);
  if (error != GRPC_ERROR_NONE) return ParseError(error);
  return ParseErrorOrConsumeAndCall<0, &ParseContext::ParseBegin>(
      parser_->table_.Add(key, &value));
}

/* finish a literal header with incremental indexing */
grpc_error* HpackParser::ParseContext::Finish_LitHdr_IncIdx() {
  const HpackTable::Row* row = parser_->table_.Lookup(parser_->index_);
  if (row == nullptr) {
    return IndexOutOfRangeError();
  }
  GRPC_STATS_INC_HPACK_RECV_LITHDR_INCIDX();
  return Finish_LitHdr_IncIdx_Tail(row->key);
}

/* finish a literal header with incremental indexing with no index */
grpc_error* HpackParser::ParseContext::Finish_LitHdr_IncIdx_V() {
  GRPC_STATS_INC_HPACK_RECV_LITHDR_INCIDX_V();
  return Finish_LitHdr_IncIdx_Tail(KeyFromName(parser_->key_.Take(true)));
}

/* parse a literal header with incremental indexing; index < 63 */
grpc_error* HpackParser::ParseContext::Parse_LitHdr_IncIdx() {
  static const State and_then[] = {
      &ParseContext::ParseValueStringWithIndexedKey,
      &ParseContext::Finish_LitHdr_IncIdx};
  parser_->dynamic_table_update_allowed_ = 0;
  parser_->next_state_ = and_then;
  parser_->index_ = (*cur_) & 0x3f;
  return ConsumeAndCall<1, &ParseContext::ParseStringPrefix>();
}

/* parse a literal header with incremental indexing; index >= 63 */
grpc_error* HpackParser::ParseContext::Parse_LitHdr_IncIdx_X() {
  static const State and_then[] = {
      &ParseContext::ParseStringPrefix,
      &ParseContext::ParseValueStringWithIndexedKey,
      &ParseContext::Finish_LitHdr_IncIdx};
  parser_->dynamic_table_update_allowed_ = 0;
  parser_->next_state_ = and_then;
  parser_->index_ = 0x3f;
  parser_->parsing_.value = &parser_->index_;
  return ConsumeAndCall<1, &ParseContext::ParseValue0>();
}

/* parse a literal header with incremental indexing; index = 0 */
grpc_error* HpackParser::ParseContext::Parse_LitHdr_IncIdx_V() {
  static const State and_then[] = {
      &ParseContext::ParseKeyString, &ParseContext::ParseStringPrefix,
      &ParseContext::ParseValueStringWithLiteralKey,
      &ParseContext::Finish_LitHdr_IncIdx_V};
  parser_->dynamic_table_update_allowed_ = 0;
  parser_->next_state_ = and_then;
  return ConsumeAndCall<1, &ParseContext::ParseStringPrefix>();
}

/* finish a literal header without incremental indexing */
grpc_error* HpackParser::ParseContext::Finish_LitHdr_NotIdx() {
  GRPC_STATS_INC_HPACK_RECV_LITHDR_NOTIDX();
  const HpackTable::Row* row = parser_->table_.Lookup(parser_->index_);
  if (row == nullptr) {
    return IndexOutOfRangeError();
  }
  return ParseErrorOrConsumeAndCall<0, &ParseContext::ParseBegin>(
      row->key->ParseIntoCollection(parser_->value_.Take(false),
                                    parsing_metadata_));
}

/* finish a literal header without incremental indexing with index = 0 */
grpc_error* HpackParser::ParseContext::Finish_LitHdr_NotIdx_V() {
  GRPC_STATS_INC_HPACK_RECV_LITHDR_NOTIDX_V();
  return ParseErrorOrConsumeAndCall<0, &ParseContext::ParseBegin>(
      KeyFromName(parser_->key_.Take(true))
          ->ParseIntoCollection(parser_->value_.Take(false),
                                parsing_metadata_));
}

/* parse a literal header without incremental indexing; index < 15 */
grpc_error* HpackParser::ParseContext::Parse_LitHdr_NotIdx() {
  static const State and_then[] = {
      &ParseContext::ParseValueStringWithIndexedKey,
      &ParseContext::Finish_LitHdr_IncIdx};
  parser_->dynamic_table_update_allowed_ = 0;
  parser_->next_state_ = and_then;
  parser_->index_ = (*cur_) & 0xf;
  return ConsumeAndCall<1, &ParseContext::ParseStringPrefix>();
}

/* parse a literal header without incremental indexing; index >= 15 */
grpc_error* HpackParser::ParseContext::Parse_LitHdr_NotIdx_X() {
  static const State and_then[] = {
      &ParseContext::ParseStringPrefix,
      &ParseContext::ParseValueStringWithIndexedKey,
      &ParseContext::Finish_LitHdr_NotIdx};
  parser_->dynamic_table_update_allowed_ = 0;
  parser_->next_state_ = and_then;
  parser_->index_ = 0xf;
  parser_->parsing_.value = &parser_->index_;
  return ConsumeAndCall<1, &ParseContext::ParseValue0>();
}

/* parse a literal header without incremental indexing; index == 0 */
grpc_error* HpackParser::ParseContext::Parse_LitHdr_NotIdx_V() {
  static const State and_then[] = {
      &ParseContext::ParseKeyString, &ParseContext::ParseStringPrefix,
      &ParseContext::ParseValueStringWithLiteralKey,
      &ParseContext::Finish_LitHdr_NotIdx_V};
  parser_->dynamic_table_update_allowed_ = 0;
  parser_->next_state_ = and_then;
  return ConsumeAndCall<1, &ParseContext::ParseStringPrefix>();
}

/* finish a literal header that is never indexed */
grpc_error* HpackParser::ParseContext::Finish_LitHdr_NvrIdx() {
  GRPC_STATS_INC_HPACK_RECV_LITHDR_NVRIDX();
  const HpackTable::Row* row = parser_->table_.Lookup(parser_->index_);
  if (row == nullptr) {
    return IndexOutOfRangeError();
  }
  return ParseErrorOrConsumeAndCall<0, &ParseContext::ParseBegin>(
      row->key->ParseIntoCollection(parser_->value_.Take(false),
                                    parsing_metadata_));
}

/* finish a literal header that is never indexed with an extra value */
grpc_error* HpackParser::ParseContext::Finish_LitHdr_NvrIdx_V() {
  GRPC_STATS_INC_HPACK_RECV_LITHDR_NVRIDX_V();
  return ParseErrorOrConsumeAndCall<0, &ParseContext::ParseBegin>(
      KeyFromName(parser_->key_.Take(true))
          ->ParseIntoCollection(parser_->value_.Take(false),
                                parsing_metadata_));
}

/* parse a literal header that is never indexed; index < 15 */
grpc_error* HpackParser::ParseContext::Parse_LitHdr_NvrIdx() {
  static const State and_then[] = {
      &ParseContext::ParseValueStringWithIndexedKey,
      &ParseContext::Finish_LitHdr_NvrIdx};
  parser_->dynamic_table_update_allowed_ = 0;
  parser_->next_state_ = and_then;
  parser_->index_ = (*cur_) & 0xf;
  return ConsumeAndCall<1, &ParseContext::ParseStringPrefix>();
}

/* parse a literal header that is never indexed; index >= 15 */
grpc_error* HpackParser::ParseContext::Parse_LitHdr_NvrIdx_X() {
  static const State and_then[] = {
      &ParseContext::ParseStringPrefix,
      &ParseContext::ParseValueStringWithIndexedKey,
      &ParseContext::Finish_LitHdr_NvrIdx};
  parser_->dynamic_table_update_allowed_ = 0;
  parser_->next_state_ = and_then;
  parser_->index_ = 0xf;
  parser_->parsing_.value = &parser_->index_;
  return ConsumeAndCall<1, &ParseContext::ParseValue0>();
}

/* parse a literal header that is never indexed; index == 0 */
grpc_error* HpackParser::ParseContext::Parse_LitHdr_NvrIdx_V() {
  static const State and_then[] = {
      &ParseContext::ParseKeyString, &ParseContext::ParseStringPrefix,
      &ParseContext::ParseValueStringWithLiteralKey,
      &ParseContext::Finish_LitHdr_NvrIdx_V};
  parser_->dynamic_table_update_allowed_ = 0;
  parser_->next_state_ = and_then;
  return ConsumeAndCall<1, &ParseContext::ParseStringPrefix>();
}

/* finish parsing a max table size change */
grpc_error* HpackParser::ParseContext::FinishMaxTblSize() {
  if (grpc_http_trace.enabled()) {
    gpr_log(GPR_INFO, "MAX TABLE SIZE: %d", parser_->index_);
  }
  return ParseErrorOrConsumeAndCall<1, &ParseContext::ParseBegin>(
      parser_->table_.SetCurrentTableSize(parser_->index_));
}

grpc_error* HpackParser::ParseContext::TooManyTableSizeChanges() {
  return ParseError(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "More than two max table size changes in a single frame"));
}

/* parse a max table size change, max size < 15 */
grpc_error* HpackParser::ParseContext::ParseMaxTableSize() {
  if (!parser_->dynamic_table_update_allowed_) {
    return TooManyTableSizeChanges();
  }
  parser_->dynamic_table_update_allowed_--;
  parser_->index_ = (*cur_) & 0x1f;
  return FinishMaxTblSize();
}

/* parse a max table size change, max size >= 15 */
grpc_error* HpackParser::ParseContext::ParseMaxTableSize_X() {
  static const State and_then[] = {&ParseContext::FinishMaxTblSize};
  if (parser_->dynamic_table_update_allowed_ == 0) {
    return TooManyTableSizeChanges();
  }
  parser_->dynamic_table_update_allowed_--;
  parser_->next_state_ = and_then;
  parser_->index_ = 0x1f;
  parser_->parsing_.value = &parser_->index_;
  return ConsumeAndCall<1, &ParseContext::ParseValue0>();
}

grpc_error* HpackParser::ParseContext::ParseIllegalOp() {
  char* msg;
  gpr_asprintf(&msg, "Illegal hpack op code %d", *cur_);
  grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
  gpr_free(msg);
  return ParseError(err);
}

/* parse the 1st byte of a varint into p->parsing.value
   no overflow is possible */
grpc_error* HpackParser::ParseContext::ParseValue0() {
  if (IsEndOfSlice()) return FinishInState(&ParseContext::ParseValue0);
  *parser_->parsing_.value += (*cur_) & 0x7f;
  return ConsumeAndCallIfOrNext<1, &ParseContext::ParseValue1>((*cur_) & 0x80);
}

/* parse the 2nd byte of a varint into p->parsing.value
   no overflow is possible */
grpc_error* HpackParser::ParseContext::ParseValue1() {
  if (IsEndOfSlice()) return FinishInState(&ParseContext::ParseValue1);
  *parser_->parsing_.value += (((uint32_t)*cur_) & 0x7f) << 7;
  return ConsumeAndCallIfOrNext<1, &ParseContext::ParseValue2>((*cur_) & 0x80);
}

/* parse the 3rd byte of a varint into p->parsing.value
   no overflow is possible */
grpc_error* HpackParser::ParseContext::ParseValue2() {
  if (IsEndOfSlice()) return FinishInState(&ParseContext::ParseValue2);
  *parser_->parsing_.value += (((uint32_t)*cur_) & 0x7f) << 14;
  return ConsumeAndCallIfOrNext<1, &ParseContext::ParseValue3>((*cur_) & 0x80);
}

/* parse the 4th byte of a varint into p->parsing.value
   no overflow is possible */
grpc_error* HpackParser::ParseContext::ParseValue3() {
  if (IsEndOfSlice()) return FinishInState(&ParseContext::ParseValue3);
  *parser_->parsing_.value += (((uint32_t)*cur_) & 0x7f) << 21;
  return ConsumeAndCallIfOrNext<1, &ParseContext::ParseValue4>((*cur_) & 0x80);
}

grpc_error* HpackParser::ParseContext::IntegerOverflowError(const char* where) {
  char* msg;
  gpr_asprintf(&msg,
               "integer overflow in hpack integer decoding: have 0x%08x, "
               "got byte 0x%02x %s",
               *parser_->parsing_.value, *cur_, where);
  grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
  gpr_free(msg);
  return ParseError(err);
}

/* parse the 5th byte of a varint into p->parsing.value
   depending on the byte, we may overflow, and care must be taken */
grpc_error* HpackParser::ParseContext::ParseValue4() {
  if (IsEndOfSlice()) return FinishInState(&ParseContext::ParseValue4);

  const uint8_t c = (*cur_) & 0x7f;
  if (c > 0xf) {
    return IntegerOverflowError("on byte 5");
  }

  const uint32_t cur_value = *parser_->parsing_.value;
  const uint32_t add_value = ((uint32_t)c) << 28;
  if (add_value > 0xffffffffu - cur_value) {
    return IntegerOverflowError("on byte 5");
  }

  *parser_->parsing_.value = cur_value + add_value;
  return ConsumeAndCallIfOrNext<1, &ParseContext::ParseValue5Up>((*cur_) &
                                                                 0x80);
}

/* parse any trailing bytes in a varint: it's possible to append an arbitrary
   number of 0x80's and not affect the value - a zero will terminate - and
   anything else will overflow */
grpc_error* HpackParser::ParseContext::ParseValue5Up() {
  while (cur_ != end_ && *cur_ == 0x80) {
    ++cur_;
  }

  if (IsEndOfSlice()) return FinishInState(&ParseContext::ParseValue5Up);
  if (*cur_ == 0) return ConsumeAndCallNext(1);
  return IntegerOverflowError("after byte 5");
}

/* parse a string prefix */
grpc_error* HpackParser::ParseContext::ParseStringPrefix() {
  if (IsEndOfSlice()) return FinishInState(&ParseContext::ParseStringPrefix);

  parser_->strlen_ = (*cur_) & 0x7f;
  parser_->huff_ = (*cur_) >> 7;
  parser_->parsing_.value = &parser_->strlen_;
  return ConsumeAndCallIfOrNext<1, &ParseContext::ParseValue0>(*cur_ == 0x7f);
}

/* append some bytes to a string */
void HpackParser::ParserString::AppendBytes(const uint8_t* data,
                                            size_t length) {
  if (length == 0) return;
  if (length + data_.copied.length > data_.copied.capacity) {
    GPR_ASSERT(data_.copied.length + length <= UINT32_MAX);
    data_.copied.capacity = (uint32_t)(data_.copied.length + length);
    data_.copied.str =
        (char*)gpr_realloc(data_.copied.str, data_.copied.capacity);
  }
  memcpy(data_.copied.str + data_.copied.length, data, length);
  GPR_ASSERT(length <= UINT32_MAX - data_.copied.length);
  data_.copied.length += (uint32_t)length;
}

grpc_error* HpackParser::ParseContext::AppendString(const uint8_t* beg,
                                                    const uint8_t* end) const {
  const uint8_t* cur = beg;
  ParserString* str = parser_->parsing_.str;
  uint32_t bits;
  uint8_t decoded[3];
  switch (parser_->binary_) {
    case BinaryParseState::NOT_BINARY:
      str->AppendBytes(cur, end - cur);
      return GRPC_ERROR_NONE;
    case BinaryParseState::BINARY_BEGIN:
      if (cur == end) {
        parser_->binary_ = BinaryParseState::BINARY_BEGIN;
        return GRPC_ERROR_NONE;
      }
      if (*cur == 0) {
        /* 'true-binary' case */
        ++cur;
        parser_->binary_ = BinaryParseState::NOT_BINARY;
        GRPC_STATS_INC_HPACK_RECV_BINARY();
        str->AppendBytes(cur, end - cur);
        return GRPC_ERROR_NONE;
      }
      GRPC_STATS_INC_HPACK_RECV_BINARY_BASE64();
    /* fallthrough */
    b64_byte0:
    case BinaryParseState::B64_BYTE0:
      if (cur == end) {
        parser_->binary_ = BinaryParseState::B64_BYTE0;
        return GRPC_ERROR_NONE;
      }
      bits = inverse_base64_[*cur];
      ++cur;
      if (bits == 255) {
        return ParseError(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Illegal base64 character"));
      } else if (bits == 64) {
        goto b64_byte0;
      }
      parser_->base64_buffer_ = bits << 18;
    /* fallthrough */
    b64_byte1:
    case BinaryParseState::B64_BYTE1:
      if (cur == end) {
        parser_->binary_ = BinaryParseState::B64_BYTE1;
        return GRPC_ERROR_NONE;
      }
      bits = inverse_base64_[*cur];
      ++cur;
      if (bits == 255) {
        return ParseError(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Illegal base64 character"));
      } else if (bits == 64) {
        goto b64_byte1;
      }
      parser_->base64_buffer_ |= bits << 12;
    /* fallthrough */
    b64_byte2:
    case BinaryParseState::B64_BYTE2:
      if (cur == end) {
        parser_->binary_ = BinaryParseState::B64_BYTE2;
        return GRPC_ERROR_NONE;
      }
      bits = inverse_base64_[*cur];
      ++cur;
      if (bits == 255) {
        return ParseError(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Illegal base64 character"));
      } else if (bits == 64) {
        goto b64_byte2;
      }
      parser_->base64_buffer_ |= bits << 6;
    /* fallthrough */
    b64_byte3:
    case BinaryParseState::B64_BYTE3:
      if (IsEndOfSlice()) {
        parser_->binary_ = BinaryParseState::B64_BYTE3;
        return GRPC_ERROR_NONE;
      }
      bits = inverse_base64_[*cur];
      ++cur;
      if (bits == 255) {
        return ParseError(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Illegal base64 character"));
      } else if (bits == 64) {
        goto b64_byte3;
      }
      parser_->base64_buffer_ |= bits;
      bits = parser_->base64_buffer_;
      decoded[0] = (uint8_t)(bits >> 16);
      decoded[1] = (uint8_t)(bits >> 8);
      decoded[2] = (uint8_t)(bits);
      str->AppendBytes(decoded, 3);
      goto b64_byte0;
  }
  GPR_UNREACHABLE_CODE(return ParseError(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Should never reach here")));
}

grpc_error* HpackParser::ParseContext::FinishStr() {
  ParserString* str = parser_->parsing_.str;
  uint8_t decoded[2];
  uint32_t bits;
  switch (parser_->binary_) {
    case BinaryParseState::NOT_BINARY:
      break;
    case BinaryParseState::BINARY_BEGIN:
      break;
    case BinaryParseState::B64_BYTE0:
      break;
    case BinaryParseState::B64_BYTE1:
      return ParseError(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "illegal base64 encoding")); /* illegal encoding */
    case BinaryParseState::B64_BYTE2:
      bits = parser_->base64_buffer_;
      if (bits & 0xffff) {
        char* msg;
        gpr_asprintf(&msg, "trailing bits in base64 encoding: 0x%04x",
                     bits & 0xffff);
        grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
        gpr_free(msg);
        return ParseError(err);
      }
      decoded[0] = (uint8_t)(bits >> 16);
      str->AppendBytes(decoded, 1);
      break;
    case BinaryParseState::B64_BYTE3:
      bits = parser_->base64_buffer_;
      if (bits & 0xff) {
        char* msg;
        gpr_asprintf(&msg, "trailing bits in base64 encoding: 0x%02x",
                     bits & 0xff);
        grpc_error* err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
        gpr_free(msg);
        return ParseError(err);
      }
      decoded[0] = (uint8_t)(bits >> 16);
      decoded[1] = (uint8_t)(bits >> 8);
      str->AppendBytes(decoded, 2);
      break;
  }
  return GRPC_ERROR_NONE;
}

/* decode a nibble from a huffman encoded stream */
grpc_error* HpackParser::ParseContext::HuffNibble(uint8_t nibble) const {
  int16_t emit = emit_sub_tbl_[16 * emit_tbl_[parser_->huff_state_] + nibble];
  int16_t next = next_sub_tbl_[16 * next_tbl_[parser_->huff_state_] + nibble];
  if (emit != -1) {
    if (emit >= 0 && emit < 256) {
      uint8_t c = (uint8_t)emit;
      grpc_error* err = AppendString(&c, (&c) + 1);
      if (err != GRPC_ERROR_NONE) return err;
    } else {
      assert(emit == 256);
    }
  }
  parser_->huff_state_ = next;
  return GRPC_ERROR_NONE;
}

/* decode full bytes from a huffman encoded stream */
grpc_error* HpackParser::ParseContext::AddHuffBytes(const uint8_t* beg,
                                                    const uint8_t* end) const {
  for (const uint8_t* cur = beg; cur != end; ++cur) {
    grpc_error* err = HuffNibble(*cur >> 4);
    if (err != GRPC_ERROR_NONE) return ParseError(err);
    err = HuffNibble(*cur & 0xf);
    if (err != GRPC_ERROR_NONE) return ParseError(err);
  }
  return GRPC_ERROR_NONE;
}

/* decode some string bytes based on the current decoding mode
   (huffman or not) */
grpc_error* HpackParser::ParseContext::AddStrBytes(const uint8_t* beg,
                                                   const uint8_t* end) const {
  if (parser_->huff_) {
    return AddHuffBytes(beg, end);
  } else {
    return AppendString(beg, end);
  }
}

/* parse a string - tries to do large chunks at a time */
grpc_error* HpackParser::ParseContext::ParseString() {
  size_t remaining = parser_->strlen_ - parser_->strgot_;
  size_t given = (size_t)(end_ - cur_);
  if (remaining <= given) {
    grpc_error* err = AddStrBytes(cur_, cur_ + remaining);
    if (err != GRPC_ERROR_NONE) return ParseError(err);
    err = FinishStr();
    if (err != GRPC_ERROR_NONE) return ParseError(err);
    return ConsumeAndCallNext(remaining);
  } else {
    grpc_error* err = AddStrBytes(cur_, cur_ + given);
    if (err != GRPC_ERROR_NONE) return ParseError(err);
    GPR_ASSERT(given <= UINT32_MAX - parser_->strgot_);
    parser_->strgot_ += (uint32_t)given;
    return FinishInState(&ParseContext::ParseString);
  }
}

void HpackParser::ParserString::ResetReferenced(grpc_slice_refcount* refcount,
                                                const uint8_t* beg,
                                                size_t len) {
  assert(state_ == State::EMPTY);
  state_ = State::COPIED;
  data_.referenced.refcount = refcount;
  data_.referenced.data.refcounted.bytes = const_cast<uint8_t*>(beg);
  data_.referenced.data.refcounted.length = len;
  grpc_slice_ref_internal(data_.referenced);
}

void HpackParser::ParserString::ResetCopied() {
  assert(state_ == State::EMPTY);
  state_ = State::REFERENCED;
  data_.copied.length = 0;
}

/* begin parsing a string - performs setup, calls parse_string */
grpc_error* HpackParser::ParseContext::BeginParseString(BinaryParseState binary,
                                                        ParserString* str) {
  if (!parser_->huff_ && binary == BinaryParseState::NOT_BINARY &&
      (end_ - cur_) >= (intptr_t)parser_->strlen_ &&
      current_slice_refcount_ != nullptr) {
    GRPC_STATS_INC_HPACK_RECV_UNCOMPRESSED();
    str->ResetReferenced(current_slice_refcount_, cur_, parser_->strlen_);
    return ConsumeAndCallNext(parser_->strlen_);
  }
  parser_->strgot_ = 0;
  str->ResetCopied();
  parser_->parsing_.str = str;
  parser_->huff_state_ = 0;
  parser_->binary_ = binary;
  switch (parser_->binary_) {
    case BinaryParseState::NOT_BINARY:
      if (parser_->huff_) {
        GRPC_STATS_INC_HPACK_RECV_HUFFMAN();
      } else {
        GRPC_STATS_INC_HPACK_RECV_UNCOMPRESSED();
      }
      break;
    case BinaryParseState::BINARY_BEGIN:
      /* stats incremented later: don't know true binary or not */
      break;
    default:
      abort();
  }
  return ParseString();
}

/* parse the key string */
grpc_error* HpackParser::ParseContext::ParseKeyString() {
  return BeginParseString(BinaryParseState::NOT_BINARY, &parser_->key_);
}

/* check if a key represents a binary header or not */

static bool is_binary_literal_header(grpc_chttp2_hpack_parser* p) {
  return grpc_is_binary_header(
      p->key.copied ? grpc_slice_from_static_buffer(p->key.data.copied.str,
                                                    p->key.data.copied.length)
                    : p->key.data.referenced);
}

static grpc_error* is_binary_indexed_header(grpc_chttp2_hpack_parser* p,
                                            bool* is) {
  grpc_mdelem elem = grpc_chttp2_hptbl_lookup(&p->table, p->index);
  if (GRPC_MDISNULL(elem)) {
    return grpc_error_set_int(
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                               "Invalid HPACK index received"),
                           GRPC_ERROR_INT_INDEX, (intptr_t)p->index),
        GRPC_ERROR_INT_SIZE, (intptr_t)p->table.num_ents);
  }
  *is = grpc_is_binary_header(GRPC_MDKEY(elem));
  return GRPC_ERROR_NONE;
}

/* parse the value string */
static grpc_error* parse_value_string(grpc_chttp2_hpack_parser* p,
                                      const uint8_t* cur, const uint8_t* end,
                                      bool is_binary) {
  return begin_parse_string(p, cur, end, is_binary ? BINARY_BEGIN : NOT_BINARY,
                            &p->value);
}

static grpc_error* parse_value_string_with_indexed_key(
    grpc_chttp2_hpack_parser* p, const uint8_t* cur, const uint8_t* end) {
  bool is_binary = false;
  grpc_error* err = is_binary_indexed_header(p, &is_binary);
  if (err != GRPC_ERROR_NONE) return parse_error(p, cur, end, err);
  return parse_value_string(p, cur, end, is_binary);
}

static grpc_error* parse_value_string_with_literal_key(
    grpc_chttp2_hpack_parser* p, const uint8_t* cur, const uint8_t* end) {
  return parse_value_string(p, cur, end, is_binary_literal_header(p));
}

/* jump table of parse state functions -- order must match first_byte_type
   above */
const HpackParser::State HpackParser::ParseContext::first_byte_action_[] = {
    &HpackParser::ParseContext::Parse_Indexed,
    &HpackParser::ParseContext::Parse_Indexed_X,
    &HpackParser::ParseContext::Parse_LitHdr_IncIdx,
    &HpackParser::ParseContext::Parse_LitHdr_IncIdx_X,
    &HpackParser::ParseContext::Parse_LitHdr_IncIdx_V,
    &HpackParser::ParseContext::Parse_LitHdr_NotIdx,
    &HpackParser::ParseContext::Parse_LitHdr_NotIdx_X,
    &HpackParser::ParseContext::Parse_LitHdr_NotIdx_V,
    &HpackParser::ParseContext::Parse_LitHdr_NvrIdx,
    &HpackParser::ParseContext::Parse_LitHdr_NvrIdx_X,
    &HpackParser::ParseContext::Parse_LitHdr_NvrIdx_V,
    &HpackParser::ParseContext::ParseMaxTableSize,
    &HpackParser::ParseContext::ParseMaxTableSize_X,
    &HpackParser::ParseContext::ParseIllegalOp};

/* indexes the first byte to a parse state function - generated by
   gen_hpack_tables.c */
const HpackParser::ParseContext::FirstByteType
    HpackParser::ParseContext::first_byte_lut_[256] = {
        FirstByteType::LITHDR_NOTIDX_V, FirstByteType::LITHDR_NOTIDX,
        FirstByteType::LITHDR_NOTIDX,   FirstByteType::LITHDR_NOTIDX,
        FirstByteType::LITHDR_NOTIDX,   FirstByteType::LITHDR_NOTIDX,
        FirstByteType::LITHDR_NOTIDX,   FirstByteType::LITHDR_NOTIDX,
        FirstByteType::LITHDR_NOTIDX,   FirstByteType::LITHDR_NOTIDX,
        FirstByteType::LITHDR_NOTIDX,   FirstByteType::LITHDR_NOTIDX,
        FirstByteType::LITHDR_NOTIDX,   FirstByteType::LITHDR_NOTIDX,
        FirstByteType::LITHDR_NOTIDX,   FirstByteType::LITHDR_NOTIDX_X,
        FirstByteType::LITHDR_NVRIDX_V, FirstByteType::LITHDR_NVRIDX,
        FirstByteType::LITHDR_NVRIDX,   FirstByteType::LITHDR_NVRIDX,
        FirstByteType::LITHDR_NVRIDX,   FirstByteType::LITHDR_NVRIDX,
        FirstByteType::LITHDR_NVRIDX,   FirstByteType::LITHDR_NVRIDX,
        FirstByteType::LITHDR_NVRIDX,   FirstByteType::LITHDR_NVRIDX,
        FirstByteType::LITHDR_NVRIDX,   FirstByteType::LITHDR_NVRIDX,
        FirstByteType::LITHDR_NVRIDX,   FirstByteType::LITHDR_NVRIDX,
        FirstByteType::LITHDR_NVRIDX,   FirstByteType::LITHDR_NVRIDX_X,
        FirstByteType::ILLEGAL,         FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE,
        FirstByteType::MAX_TBL_SIZE,    FirstByteType::MAX_TBL_SIZE_X,
        FirstByteType::LITHDR_INCIDX_V, FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX,
        FirstByteType::LITHDR_INCIDX,   FirstByteType::LITHDR_INCIDX_X,
        FirstByteType::ILLEGAL,         FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD,
        FirstByteType::INDEXED_FIELD,   FirstByteType::INDEXED_FIELD_X,
};

namespace {}
/* state table for huffman decoding: given a state, gives an index/16 into
   next_sub_tbl. Taking that index and adding the value of the nibble being
   considered returns the next state.

   generated by gen_hpack_tables.c */
const uint8_t HpackParser::ParseContext::next_tbl_[256] = {
    0,  1,  2,  3,  4,  1,  2, 5,  6,  1, 7,  8,  1,  3,  3,  9,  10, 11, 1,  1,
    1,  12, 1,  2,  13, 1,  1, 1,  1,  1, 1,  1,  1,  1,  1,  1,  1,  1,  1,  2,
    14, 1,  15, 16, 1,  17, 1, 15, 2,  7, 3,  18, 19, 1,  1,  1,  1,  20, 1,  1,
    1,  1,  1,  1,  1,  1,  1, 1,  15, 2, 2,  7,  21, 1,  22, 1,  1,  1,  1,  1,
    1,  1,  1,  15, 2,  2,  2, 2,  2,  2, 23, 24, 25, 1,  1,  1,  1,  2,  2,  2,
    26, 3,  3,  27, 10, 28, 1, 1,  1,  1, 1,  1,  2,  3,  29, 10, 30, 1,  1,  1,
    1,  1,  1,  1,  1,  1,  1, 1,  1,  1, 1,  31, 1,  1,  1,  1,  1,  1,  1,  2,
    2,  2,  2,  2,  2,  2,  2, 32, 1,  1, 15, 33, 1,  34, 35, 9,  36, 1,  1,  1,
    1,  1,  1,  1,  37, 1,  1, 1,  1,  1, 1,  2,  2,  2,  2,  2,  2,  2,  26, 9,
    38, 1,  1,  1,  1,  1,  1, 1,  15, 2, 2,  2,  2,  26, 3,  3,  39, 1,  1,  1,
    1,  1,  1,  1,  1,  1,  1, 1,  2,  2, 2,  2,  2,  2,  7,  3,  3,  3,  40, 2,
    41, 1,  1,  1,  42, 43, 1, 1,  44, 1, 1,  1,  1,  15, 2,  2,  2,  2,  2,  2,
    3,  3,  3,  45, 46, 1,  1, 2,  2,  2, 35, 3,  3,  18, 47, 2,
};

/* next state, based upon current state and the current nibble: see above.
   generated by gen_hpack_tables.c */
const int16_t HpackParser::ParseContext::next_sub_tbl_[48 * 16] = {
    1,   204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217,
    218, 2,   6,   10,  13,  14,  15,  16,  17,  2,   6,   10,  13,  14,  15,
    16,  17,  3,   7,   11,  24,  3,   7,   11,  24,  3,   7,   11,  24,  3,
    7,   11,  24,  4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,
    4,   8,   4,   8,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   5,
    199, 200, 201, 202, 203, 4,   8,   4,   8,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   9,   133, 134, 135, 136, 137, 138, 139, 140,
    141, 142, 143, 144, 145, 146, 147, 3,   7,   11,  24,  3,   7,   11,  24,
    4,   8,   4,   8,   4,   8,   4,   8,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   12,  132, 4,   8,   4,   8,   4,   8,
    4,   8,   4,   8,   4,   8,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   18,  19,  20,  21,  4,   8,   4,
    8,   4,   8,   4,   8,   4,   8,   0,   0,   0,   22,  23,  91,  25,  26,
    27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  3,
    7,   11,  24,  3,   7,   11,  24,  0,   0,   0,   0,   0,   41,  42,  43,
    2,   6,   10,  13,  14,  15,  16,  17,  3,   7,   11,  24,  3,   7,   11,
    24,  4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   0,   0,
    44,  45,  2,   6,   10,  13,  14,  15,  16,  17,  46,  47,  48,  49,  50,
    51,  52,  57,  4,   8,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   53,  54,  55,  56,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,
    68,  69,  70,  71,  72,  74,  0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   73,  75,  76,  77,  78,  79,  80,  81,  82,
    83,  84,  85,  86,  87,  88,  89,  90,  3,   7,   11,  24,  3,   7,   11,
    24,  3,   7,   11,  24,  0,   0,   0,   0,   3,   7,   11,  24,  3,   7,
    11,  24,  4,   8,   4,   8,   0,   0,   0,   92,  0,   0,   0,   93,  94,
    95,  96,  97,  98,  99,  100, 101, 102, 103, 104, 105, 3,   7,   11,  24,
    4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,
    8,   4,   8,   4,   8,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 4,
    8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   0,   0,
    0,   117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130,
    131, 2,   6,   10,  13,  14,  15,  16,  17,  4,   8,   4,   8,   4,   8,
    4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   148,
    149, 150, 151, 3,   7,   11,  24,  4,   8,   4,   8,   0,   0,   0,   0,
    0,   0,   152, 153, 3,   7,   11,  24,  3,   7,   11,  24,  3,   7,   11,
    24,  154, 155, 156, 164, 3,   7,   11,  24,  3,   7,   11,  24,  3,   7,
    11,  24,  4,   8,   4,   8,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    157, 158, 159, 160, 161, 162, 163, 165, 166, 167, 168, 169, 170, 171, 172,
    173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187,
    188, 189, 190, 191, 192, 193, 194, 195, 196, 4,   8,   4,   8,   4,   8,
    4,   8,   4,   8,   4,   8,   4,   8,   197, 198, 4,   8,   4,   8,   4,
    8,   4,   8,   0,   0,   0,   0,   0,   0,   219, 220, 3,   7,   11,  24,
    4,   8,   4,   8,   4,   8,   0,   0,   221, 222, 223, 224, 3,   7,   11,
    24,  3,   7,   11,  24,  4,   8,   4,   8,   4,   8,   225, 228, 4,   8,
    4,   8,   4,   8,   0,   0,   0,   0,   0,   0,   0,   0,   226, 227, 229,
    230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244,
    4,   8,   4,   8,   4,   8,   4,   8,   4,   8,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   245, 246, 247, 248, 249, 250, 251, 252,
    253, 254, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   255,
};

/* emission table: indexed like next_tbl, ultimately gives the byte to be
   emitted, or -1 for no byte, or 256 for end of stream

   generated by gen_hpack_tables.c */
const uint16_t HpackParser::ParseContext::emit_tbl_[256] = {
    0,   1,   2,   3,   4,   5,   6,   7,   0,   8,   9,   10,  11,  12,  13,
    14,  15,  16,  17,  18,  19,  20,  21,  22,  0,   23,  24,  25,  26,  27,
    28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,
    43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  0,   55,  56,
    57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  0,
    71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,
    86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,  98,  99,  100,
    101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115,
    116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130,
    131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145,
    146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 0,
    160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174,
    0,   175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188,
    189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203,
    204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218,
    219, 220, 221, 0,   222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232,
    233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247,
    248,
};

/* generated by gen_hpack_tables.c */
const int16_t HpackParser::ParseContext::emit_sub_tbl_[249 * 16] = {
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  48,  48,  48,  48,  48,  48,  48,  48,  49,  49,  49,  49,  49,  49,
    49,  49,  48,  48,  48,  48,  49,  49,  49,  49,  50,  50,  50,  50,  97,
    97,  97,  97,  48,  48,  49,  49,  50,  50,  97,  97,  99,  99,  101, 101,
    105, 105, 111, 111, 48,  49,  50,  97,  99,  101, 105, 111, 115, 116, -1,
    -1,  -1,  -1,  -1,  -1,  32,  32,  32,  32,  32,  32,  32,  32,  37,  37,
    37,  37,  37,  37,  37,  37,  99,  99,  99,  99,  101, 101, 101, 101, 105,
    105, 105, 105, 111, 111, 111, 111, 115, 115, 116, 116, 32,  37,  45,  46,
    47,  51,  52,  53,  54,  55,  56,  57,  61,  61,  61,  61,  61,  61,  61,
    61,  65,  65,  65,  65,  65,  65,  65,  65,  115, 115, 115, 115, 116, 116,
    116, 116, 32,  32,  37,  37,  45,  45,  46,  46,  61,  65,  95,  98,  100,
    102, 103, 104, 108, 109, 110, 112, 114, 117, -1,  -1,  58,  58,  58,  58,
    58,  58,  58,  58,  66,  66,  66,  66,  66,  66,  66,  66,  47,  47,  51,
    51,  52,  52,  53,  53,  54,  54,  55,  55,  56,  56,  57,  57,  61,  61,
    65,  65,  95,  95,  98,  98,  100, 100, 102, 102, 103, 103, 104, 104, 108,
    108, 109, 109, 110, 110, 112, 112, 114, 114, 117, 117, 58,  66,  67,  68,
    69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,
    84,  85,  86,  87,  89,  106, 107, 113, 118, 119, 120, 121, 122, -1,  -1,
    -1,  -1,  38,  38,  38,  38,  38,  38,  38,  38,  42,  42,  42,  42,  42,
    42,  42,  42,  44,  44,  44,  44,  44,  44,  44,  44,  59,  59,  59,  59,
    59,  59,  59,  59,  88,  88,  88,  88,  88,  88,  88,  88,  90,  90,  90,
    90,  90,  90,  90,  90,  33,  33,  34,  34,  40,  40,  41,  41,  63,  63,
    39,  43,  124, -1,  -1,  -1,  35,  35,  35,  35,  35,  35,  35,  35,  62,
    62,  62,  62,  62,  62,  62,  62,  0,   0,   0,   0,   36,  36,  36,  36,
    64,  64,  64,  64,  91,  91,  91,  91,  69,  69,  69,  69,  69,  69,  69,
    69,  70,  70,  70,  70,  70,  70,  70,  70,  71,  71,  71,  71,  71,  71,
    71,  71,  72,  72,  72,  72,  72,  72,  72,  72,  73,  73,  73,  73,  73,
    73,  73,  73,  74,  74,  74,  74,  74,  74,  74,  74,  75,  75,  75,  75,
    75,  75,  75,  75,  76,  76,  76,  76,  76,  76,  76,  76,  77,  77,  77,
    77,  77,  77,  77,  77,  78,  78,  78,  78,  78,  78,  78,  78,  79,  79,
    79,  79,  79,  79,  79,  79,  80,  80,  80,  80,  80,  80,  80,  80,  81,
    81,  81,  81,  81,  81,  81,  81,  82,  82,  82,  82,  82,  82,  82,  82,
    83,  83,  83,  83,  83,  83,  83,  83,  84,  84,  84,  84,  84,  84,  84,
    84,  85,  85,  85,  85,  85,  85,  85,  85,  86,  86,  86,  86,  86,  86,
    86,  86,  87,  87,  87,  87,  87,  87,  87,  87,  89,  89,  89,  89,  89,
    89,  89,  89,  106, 106, 106, 106, 106, 106, 106, 106, 107, 107, 107, 107,
    107, 107, 107, 107, 113, 113, 113, 113, 113, 113, 113, 113, 118, 118, 118,
    118, 118, 118, 118, 118, 119, 119, 119, 119, 119, 119, 119, 119, 120, 120,
    120, 120, 120, 120, 120, 120, 121, 121, 121, 121, 121, 121, 121, 121, 122,
    122, 122, 122, 122, 122, 122, 122, 38,  38,  38,  38,  42,  42,  42,  42,
    44,  44,  44,  44,  59,  59,  59,  59,  88,  88,  88,  88,  90,  90,  90,
    90,  33,  34,  40,  41,  63,  -1,  -1,  -1,  39,  39,  39,  39,  39,  39,
    39,  39,  43,  43,  43,  43,  43,  43,  43,  43,  124, 124, 124, 124, 124,
    124, 124, 124, 35,  35,  35,  35,  62,  62,  62,  62,  0,   0,   36,  36,
    64,  64,  91,  91,  93,  93,  126, 126, 94,  125, -1,  -1,  60,  60,  60,
    60,  60,  60,  60,  60,  96,  96,  96,  96,  96,  96,  96,  96,  123, 123,
    123, 123, 123, 123, 123, 123, -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  92,
    92,  92,  92,  92,  92,  92,  92,  195, 195, 195, 195, 195, 195, 195, 195,
    208, 208, 208, 208, 208, 208, 208, 208, 128, 128, 128, 128, 130, 130, 130,
    130, 131, 131, 131, 131, 162, 162, 162, 162, 184, 184, 184, 184, 194, 194,
    194, 194, 224, 224, 224, 224, 226, 226, 226, 226, 153, 153, 161, 161, 167,
    167, 172, 172, 176, 176, 177, 177, 179, 179, 209, 209, 216, 216, 217, 217,
    227, 227, 229, 229, 230, 230, 129, 132, 133, 134, 136, 146, 154, 156, 160,
    163, 164, 169, 170, 173, 178, 181, 185, 186, 187, 189, 190, 196, 198, 228,
    232, 233, -1,  -1,  -1,  -1,  1,   1,   1,   1,   1,   1,   1,   1,   135,
    135, 135, 135, 135, 135, 135, 135, 137, 137, 137, 137, 137, 137, 137, 137,
    138, 138, 138, 138, 138, 138, 138, 138, 139, 139, 139, 139, 139, 139, 139,
    139, 140, 140, 140, 140, 140, 140, 140, 140, 141, 141, 141, 141, 141, 141,
    141, 141, 143, 143, 143, 143, 143, 143, 143, 143, 147, 147, 147, 147, 147,
    147, 147, 147, 149, 149, 149, 149, 149, 149, 149, 149, 150, 150, 150, 150,
    150, 150, 150, 150, 151, 151, 151, 151, 151, 151, 151, 151, 152, 152, 152,
    152, 152, 152, 152, 152, 155, 155, 155, 155, 155, 155, 155, 155, 157, 157,
    157, 157, 157, 157, 157, 157, 158, 158, 158, 158, 158, 158, 158, 158, 165,
    165, 165, 165, 165, 165, 165, 165, 166, 166, 166, 166, 166, 166, 166, 166,
    168, 168, 168, 168, 168, 168, 168, 168, 174, 174, 174, 174, 174, 174, 174,
    174, 175, 175, 175, 175, 175, 175, 175, 175, 180, 180, 180, 180, 180, 180,
    180, 180, 182, 182, 182, 182, 182, 182, 182, 182, 183, 183, 183, 183, 183,
    183, 183, 183, 188, 188, 188, 188, 188, 188, 188, 188, 191, 191, 191, 191,
    191, 191, 191, 191, 197, 197, 197, 197, 197, 197, 197, 197, 231, 231, 231,
    231, 231, 231, 231, 231, 239, 239, 239, 239, 239, 239, 239, 239, 9,   9,
    9,   9,   142, 142, 142, 142, 144, 144, 144, 144, 145, 145, 145, 145, 148,
    148, 148, 148, 159, 159, 159, 159, 171, 171, 171, 171, 206, 206, 206, 206,
    215, 215, 215, 215, 225, 225, 225, 225, 236, 236, 236, 236, 237, 237, 237,
    237, 199, 199, 207, 207, 234, 234, 235, 235, 192, 193, 200, 201, 202, 205,
    210, 213, 218, 219, 238, 240, 242, 243, 255, -1,  203, 203, 203, 203, 203,
    203, 203, 203, 204, 204, 204, 204, 204, 204, 204, 204, 211, 211, 211, 211,
    211, 211, 211, 211, 212, 212, 212, 212, 212, 212, 212, 212, 214, 214, 214,
    214, 214, 214, 214, 214, 221, 221, 221, 221, 221, 221, 221, 221, 222, 222,
    222, 222, 222, 222, 222, 222, 223, 223, 223, 223, 223, 223, 223, 223, 241,
    241, 241, 241, 241, 241, 241, 241, 244, 244, 244, 244, 244, 244, 244, 244,
    245, 245, 245, 245, 245, 245, 245, 245, 246, 246, 246, 246, 246, 246, 246,
    246, 247, 247, 247, 247, 247, 247, 247, 247, 248, 248, 248, 248, 248, 248,
    248, 248, 250, 250, 250, 250, 250, 250, 250, 250, 251, 251, 251, 251, 251,
    251, 251, 251, 252, 252, 252, 252, 252, 252, 252, 252, 253, 253, 253, 253,
    253, 253, 253, 253, 254, 254, 254, 254, 254, 254, 254, 254, 2,   2,   2,
    2,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,
    6,   6,   7,   7,   7,   7,   8,   8,   8,   8,   11,  11,  11,  11,  12,
    12,  12,  12,  14,  14,  14,  14,  15,  15,  15,  15,  16,  16,  16,  16,
    17,  17,  17,  17,  18,  18,  18,  18,  19,  19,  19,  19,  20,  20,  20,
    20,  21,  21,  21,  21,  23,  23,  23,  23,  24,  24,  24,  24,  25,  25,
    25,  25,  26,  26,  26,  26,  27,  27,  27,  27,  28,  28,  28,  28,  29,
    29,  29,  29,  30,  30,  30,  30,  31,  31,  31,  31,  127, 127, 127, 127,
    220, 220, 220, 220, 249, 249, 249, 249, 10,  13,  22,  256, 93,  93,  93,
    93,  126, 126, 126, 126, 94,  94,  125, 125, 60,  96,  123, -1,  92,  195,
    208, -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  128,
    128, 128, 128, 128, 128, 128, 128, 130, 130, 130, 130, 130, 130, 130, 130,
    131, 131, 131, 131, 131, 131, 131, 131, 162, 162, 162, 162, 162, 162, 162,
    162, 184, 184, 184, 184, 184, 184, 184, 184, 194, 194, 194, 194, 194, 194,
    194, 194, 224, 224, 224, 224, 224, 224, 224, 224, 226, 226, 226, 226, 226,
    226, 226, 226, 153, 153, 153, 153, 161, 161, 161, 161, 167, 167, 167, 167,
    172, 172, 172, 172, 176, 176, 176, 176, 177, 177, 177, 177, 179, 179, 179,
    179, 209, 209, 209, 209, 216, 216, 216, 216, 217, 217, 217, 217, 227, 227,
    227, 227, 229, 229, 229, 229, 230, 230, 230, 230, 129, 129, 132, 132, 133,
    133, 134, 134, 136, 136, 146, 146, 154, 154, 156, 156, 160, 160, 163, 163,
    164, 164, 169, 169, 170, 170, 173, 173, 178, 178, 181, 181, 185, 185, 186,
    186, 187, 187, 189, 189, 190, 190, 196, 196, 198, 198, 228, 228, 232, 232,
    233, 233, 1,   135, 137, 138, 139, 140, 141, 143, 147, 149, 150, 151, 152,
    155, 157, 158, 165, 166, 168, 174, 175, 180, 182, 183, 188, 191, 197, 231,
    239, -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  9,   9,   9,
    9,   9,   9,   9,   9,   142, 142, 142, 142, 142, 142, 142, 142, 144, 144,
    144, 144, 144, 144, 144, 144, 145, 145, 145, 145, 145, 145, 145, 145, 148,
    148, 148, 148, 148, 148, 148, 148, 159, 159, 159, 159, 159, 159, 159, 159,
    171, 171, 171, 171, 171, 171, 171, 171, 206, 206, 206, 206, 206, 206, 206,
    206, 215, 215, 215, 215, 215, 215, 215, 215, 225, 225, 225, 225, 225, 225,
    225, 225, 236, 236, 236, 236, 236, 236, 236, 236, 237, 237, 237, 237, 237,
    237, 237, 237, 199, 199, 199, 199, 207, 207, 207, 207, 234, 234, 234, 234,
    235, 235, 235, 235, 192, 192, 193, 193, 200, 200, 201, 201, 202, 202, 205,
    205, 210, 210, 213, 213, 218, 218, 219, 219, 238, 238, 240, 240, 242, 242,
    243, 243, 255, 255, 203, 204, 211, 212, 214, 221, 222, 223, 241, 244, 245,
    246, 247, 248, 250, 251, 252, 253, 254, -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  2,   2,   2,   2,   2,   2,   2,
    2,   3,   3,   3,   3,   3,   3,   3,   3,   4,   4,   4,   4,   4,   4,
    4,   4,   5,   5,   5,   5,   5,   5,   5,   5,   6,   6,   6,   6,   6,
    6,   6,   6,   7,   7,   7,   7,   7,   7,   7,   7,   8,   8,   8,   8,
    8,   8,   8,   8,   11,  11,  11,  11,  11,  11,  11,  11,  12,  12,  12,
    12,  12,  12,  12,  12,  14,  14,  14,  14,  14,  14,  14,  14,  15,  15,
    15,  15,  15,  15,  15,  15,  16,  16,  16,  16,  16,  16,  16,  16,  17,
    17,  17,  17,  17,  17,  17,  17,  18,  18,  18,  18,  18,  18,  18,  18,
    19,  19,  19,  19,  19,  19,  19,  19,  20,  20,  20,  20,  20,  20,  20,
    20,  21,  21,  21,  21,  21,  21,  21,  21,  23,  23,  23,  23,  23,  23,
    23,  23,  24,  24,  24,  24,  24,  24,  24,  24,  25,  25,  25,  25,  25,
    25,  25,  25,  26,  26,  26,  26,  26,  26,  26,  26,  27,  27,  27,  27,
    27,  27,  27,  27,  28,  28,  28,  28,  28,  28,  28,  28,  29,  29,  29,
    29,  29,  29,  29,  29,  30,  30,  30,  30,  30,  30,  30,  30,  31,  31,
    31,  31,  31,  31,  31,  31,  127, 127, 127, 127, 127, 127, 127, 127, 220,
    220, 220, 220, 220, 220, 220, 220, 249, 249, 249, 249, 249, 249, 249, 249,
    10,  10,  13,  13,  22,  22,  256, 256, 67,  67,  67,  67,  67,  67,  67,
    67,  68,  68,  68,  68,  68,  68,  68,  68,  95,  95,  95,  95,  95,  95,
    95,  95,  98,  98,  98,  98,  98,  98,  98,  98,  100, 100, 100, 100, 100,
    100, 100, 100, 102, 102, 102, 102, 102, 102, 102, 102, 103, 103, 103, 103,
    103, 103, 103, 103, 104, 104, 104, 104, 104, 104, 104, 104, 108, 108, 108,
    108, 108, 108, 108, 108, 109, 109, 109, 109, 109, 109, 109, 109, 110, 110,
    110, 110, 110, 110, 110, 110, 112, 112, 112, 112, 112, 112, 112, 112, 114,
    114, 114, 114, 114, 114, 114, 114, 117, 117, 117, 117, 117, 117, 117, 117,
    58,  58,  58,  58,  66,  66,  66,  66,  67,  67,  67,  67,  68,  68,  68,
    68,  69,  69,  69,  69,  70,  70,  70,  70,  71,  71,  71,  71,  72,  72,
    72,  72,  73,  73,  73,  73,  74,  74,  74,  74,  75,  75,  75,  75,  76,
    76,  76,  76,  77,  77,  77,  77,  78,  78,  78,  78,  79,  79,  79,  79,
    80,  80,  80,  80,  81,  81,  81,  81,  82,  82,  82,  82,  83,  83,  83,
    83,  84,  84,  84,  84,  85,  85,  85,  85,  86,  86,  86,  86,  87,  87,
    87,  87,  89,  89,  89,  89,  106, 106, 106, 106, 107, 107, 107, 107, 113,
    113, 113, 113, 118, 118, 118, 118, 119, 119, 119, 119, 120, 120, 120, 120,
    121, 121, 121, 121, 122, 122, 122, 122, 38,  38,  42,  42,  44,  44,  59,
    59,  88,  88,  90,  90,  -1,  -1,  -1,  -1,  33,  33,  33,  33,  33,  33,
    33,  33,  34,  34,  34,  34,  34,  34,  34,  34,  40,  40,  40,  40,  40,
    40,  40,  40,  41,  41,  41,  41,  41,  41,  41,  41,  63,  63,  63,  63,
    63,  63,  63,  63,  39,  39,  39,  39,  43,  43,  43,  43,  124, 124, 124,
    124, 35,  35,  62,  62,  0,   36,  64,  91,  93,  126, -1,  -1,  94,  94,
    94,  94,  94,  94,  94,  94,  125, 125, 125, 125, 125, 125, 125, 125, 60,
    60,  60,  60,  96,  96,  96,  96,  123, 123, 123, 123, -1,  -1,  -1,  -1,
    92,  92,  92,  92,  195, 195, 195, 195, 208, 208, 208, 208, 128, 128, 130,
    130, 131, 131, 162, 162, 184, 184, 194, 194, 224, 224, 226, 226, 153, 161,
    167, 172, 176, 177, 179, 209, 216, 217, 227, 229, 230, -1,  -1,  -1,  -1,
    -1,  -1,  -1,  129, 129, 129, 129, 129, 129, 129, 129, 132, 132, 132, 132,
    132, 132, 132, 132, 133, 133, 133, 133, 133, 133, 133, 133, 134, 134, 134,
    134, 134, 134, 134, 134, 136, 136, 136, 136, 136, 136, 136, 136, 146, 146,
    146, 146, 146, 146, 146, 146, 154, 154, 154, 154, 154, 154, 154, 154, 156,
    156, 156, 156, 156, 156, 156, 156, 160, 160, 160, 160, 160, 160, 160, 160,
    163, 163, 163, 163, 163, 163, 163, 163, 164, 164, 164, 164, 164, 164, 164,
    164, 169, 169, 169, 169, 169, 169, 169, 169, 170, 170, 170, 170, 170, 170,
    170, 170, 173, 173, 173, 173, 173, 173, 173, 173, 178, 178, 178, 178, 178,
    178, 178, 178, 181, 181, 181, 181, 181, 181, 181, 181, 185, 185, 185, 185,
    185, 185, 185, 185, 186, 186, 186, 186, 186, 186, 186, 186, 187, 187, 187,
    187, 187, 187, 187, 187, 189, 189, 189, 189, 189, 189, 189, 189, 190, 190,
    190, 190, 190, 190, 190, 190, 196, 196, 196, 196, 196, 196, 196, 196, 198,
    198, 198, 198, 198, 198, 198, 198, 228, 228, 228, 228, 228, 228, 228, 228,
    232, 232, 232, 232, 232, 232, 232, 232, 233, 233, 233, 233, 233, 233, 233,
    233, 1,   1,   1,   1,   135, 135, 135, 135, 137, 137, 137, 137, 138, 138,
    138, 138, 139, 139, 139, 139, 140, 140, 140, 140, 141, 141, 141, 141, 143,
    143, 143, 143, 147, 147, 147, 147, 149, 149, 149, 149, 150, 150, 150, 150,
    151, 151, 151, 151, 152, 152, 152, 152, 155, 155, 155, 155, 157, 157, 157,
    157, 158, 158, 158, 158, 165, 165, 165, 165, 166, 166, 166, 166, 168, 168,
    168, 168, 174, 174, 174, 174, 175, 175, 175, 175, 180, 180, 180, 180, 182,
    182, 182, 182, 183, 183, 183, 183, 188, 188, 188, 188, 191, 191, 191, 191,
    197, 197, 197, 197, 231, 231, 231, 231, 239, 239, 239, 239, 9,   9,   142,
    142, 144, 144, 145, 145, 148, 148, 159, 159, 171, 171, 206, 206, 215, 215,
    225, 225, 236, 236, 237, 237, 199, 207, 234, 235, 192, 192, 192, 192, 192,
    192, 192, 192, 193, 193, 193, 193, 193, 193, 193, 193, 200, 200, 200, 200,
    200, 200, 200, 200, 201, 201, 201, 201, 201, 201, 201, 201, 202, 202, 202,
    202, 202, 202, 202, 202, 205, 205, 205, 205, 205, 205, 205, 205, 210, 210,
    210, 210, 210, 210, 210, 210, 213, 213, 213, 213, 213, 213, 213, 213, 218,
    218, 218, 218, 218, 218, 218, 218, 219, 219, 219, 219, 219, 219, 219, 219,
    238, 238, 238, 238, 238, 238, 238, 238, 240, 240, 240, 240, 240, 240, 240,
    240, 242, 242, 242, 242, 242, 242, 242, 242, 243, 243, 243, 243, 243, 243,
    243, 243, 255, 255, 255, 255, 255, 255, 255, 255, 203, 203, 203, 203, 204,
    204, 204, 204, 211, 211, 211, 211, 212, 212, 212, 212, 214, 214, 214, 214,
    221, 221, 221, 221, 222, 222, 222, 222, 223, 223, 223, 223, 241, 241, 241,
    241, 244, 244, 244, 244, 245, 245, 245, 245, 246, 246, 246, 246, 247, 247,
    247, 247, 248, 248, 248, 248, 250, 250, 250, 250, 251, 251, 251, 251, 252,
    252, 252, 252, 253, 253, 253, 253, 254, 254, 254, 254, 2,   2,   3,   3,
    4,   4,   5,   5,   6,   6,   7,   7,   8,   8,   11,  11,  12,  12,  14,
    14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,  20,  20,  21,  21,
    23,  23,  24,  24,  25,  25,  26,  26,  27,  27,  28,  28,  29,  29,  30,
    30,  31,  31,  127, 127, 220, 220, 249, 249, -1,  -1,  10,  10,  10,  10,
    10,  10,  10,  10,  13,  13,  13,  13,  13,  13,  13,  13,  22,  22,  22,
    22,  22,  22,  22,  22,  256, 256, 256, 256, 256, 256, 256, 256, 45,  45,
    45,  45,  45,  45,  45,  45,  46,  46,  46,  46,  46,  46,  46,  46,  47,
    47,  47,  47,  47,  47,  47,  47,  51,  51,  51,  51,  51,  51,  51,  51,
    52,  52,  52,  52,  52,  52,  52,  52,  53,  53,  53,  53,  53,  53,  53,
    53,  54,  54,  54,  54,  54,  54,  54,  54,  55,  55,  55,  55,  55,  55,
    55,  55,  56,  56,  56,  56,  56,  56,  56,  56,  57,  57,  57,  57,  57,
    57,  57,  57,  50,  50,  50,  50,  50,  50,  50,  50,  97,  97,  97,  97,
    97,  97,  97,  97,  99,  99,  99,  99,  99,  99,  99,  99,  101, 101, 101,
    101, 101, 101, 101, 101, 105, 105, 105, 105, 105, 105, 105, 105, 111, 111,
    111, 111, 111, 111, 111, 111, 115, 115, 115, 115, 115, 115, 115, 115, 116,
    116, 116, 116, 116, 116, 116, 116, 32,  32,  32,  32,  37,  37,  37,  37,
    45,  45,  45,  45,  46,  46,  46,  46,  47,  47,  47,  47,  51,  51,  51,
    51,  52,  52,  52,  52,  53,  53,  53,  53,  54,  54,  54,  54,  55,  55,
    55,  55,  56,  56,  56,  56,  57,  57,  57,  57,  61,  61,  61,  61,  65,
    65,  65,  65,  95,  95,  95,  95,  98,  98,  98,  98,  100, 100, 100, 100,
    102, 102, 102, 102, 103, 103, 103, 103, 104, 104, 104, 104, 108, 108, 108,
    108, 109, 109, 109, 109, 110, 110, 110, 110, 112, 112, 112, 112, 114, 114,
    114, 114, 117, 117, 117, 117, 58,  58,  66,  66,  67,  67,  68,  68,  69,
    69,  70,  70,  71,  71,  72,  72,  73,  73,  74,  74,  75,  75,  76,  76,
    77,  77,  78,  78,  79,  79,  80,  80,  81,  81,  82,  82,  83,  83,  84,
    84,  85,  85,  86,  86,  87,  87,  89,  89,  106, 106, 107, 107, 113, 113,
    118, 118, 119, 119, 120, 120, 121, 121, 122, 122, 38,  42,  44,  59,  88,
    90,  -1,  -1,  33,  33,  33,  33,  34,  34,  34,  34,  40,  40,  40,  40,
    41,  41,  41,  41,  63,  63,  63,  63,  39,  39,  43,  43,  124, 124, 35,
    62,  -1,  -1,  -1,  -1,  0,   0,   0,   0,   0,   0,   0,   0,   36,  36,
    36,  36,  36,  36,  36,  36,  64,  64,  64,  64,  64,  64,  64,  64,  91,
    91,  91,  91,  91,  91,  91,  91,  93,  93,  93,  93,  93,  93,  93,  93,
    126, 126, 126, 126, 126, 126, 126, 126, 94,  94,  94,  94,  125, 125, 125,
    125, 60,  60,  96,  96,  123, 123, -1,  -1,  92,  92,  195, 195, 208, 208,
    128, 130, 131, 162, 184, 194, 224, 226, -1,  -1,  153, 153, 153, 153, 153,
    153, 153, 153, 161, 161, 161, 161, 161, 161, 161, 161, 167, 167, 167, 167,
    167, 167, 167, 167, 172, 172, 172, 172, 172, 172, 172, 172, 176, 176, 176,
    176, 176, 176, 176, 176, 177, 177, 177, 177, 177, 177, 177, 177, 179, 179,
    179, 179, 179, 179, 179, 179, 209, 209, 209, 209, 209, 209, 209, 209, 216,
    216, 216, 216, 216, 216, 216, 216, 217, 217, 217, 217, 217, 217, 217, 217,
    227, 227, 227, 227, 227, 227, 227, 227, 229, 229, 229, 229, 229, 229, 229,
    229, 230, 230, 230, 230, 230, 230, 230, 230, 129, 129, 129, 129, 132, 132,
    132, 132, 133, 133, 133, 133, 134, 134, 134, 134, 136, 136, 136, 136, 146,
    146, 146, 146, 154, 154, 154, 154, 156, 156, 156, 156, 160, 160, 160, 160,
    163, 163, 163, 163, 164, 164, 164, 164, 169, 169, 169, 169, 170, 170, 170,
    170, 173, 173, 173, 173, 178, 178, 178, 178, 181, 181, 181, 181, 185, 185,
    185, 185, 186, 186, 186, 186, 187, 187, 187, 187, 189, 189, 189, 189, 190,
    190, 190, 190, 196, 196, 196, 196, 198, 198, 198, 198, 228, 228, 228, 228,
    232, 232, 232, 232, 233, 233, 233, 233, 1,   1,   135, 135, 137, 137, 138,
    138, 139, 139, 140, 140, 141, 141, 143, 143, 147, 147, 149, 149, 150, 150,
    151, 151, 152, 152, 155, 155, 157, 157, 158, 158, 165, 165, 166, 166, 168,
    168, 174, 174, 175, 175, 180, 180, 182, 182, 183, 183, 188, 188, 191, 191,
    197, 197, 231, 231, 239, 239, 9,   142, 144, 145, 148, 159, 171, 206, 215,
    225, 236, 237, -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  199, 199,
    199, 199, 199, 199, 199, 199, 207, 207, 207, 207, 207, 207, 207, 207, 234,
    234, 234, 234, 234, 234, 234, 234, 235, 235, 235, 235, 235, 235, 235, 235,
    192, 192, 192, 192, 193, 193, 193, 193, 200, 200, 200, 200, 201, 201, 201,
    201, 202, 202, 202, 202, 205, 205, 205, 205, 210, 210, 210, 210, 213, 213,
    213, 213, 218, 218, 218, 218, 219, 219, 219, 219, 238, 238, 238, 238, 240,
    240, 240, 240, 242, 242, 242, 242, 243, 243, 243, 243, 255, 255, 255, 255,
    203, 203, 204, 204, 211, 211, 212, 212, 214, 214, 221, 221, 222, 222, 223,
    223, 241, 241, 244, 244, 245, 245, 246, 246, 247, 247, 248, 248, 250, 250,
    251, 251, 252, 252, 253, 253, 254, 254, 2,   3,   4,   5,   6,   7,   8,
    11,  12,  14,  15,  16,  17,  18,  19,  20,  21,  23,  24,  25,  26,  27,
    28,  29,  30,  31,  127, 220, 249, -1,  10,  10,  10,  10,  13,  13,  13,
    13,  22,  22,  22,  22,  256, 256, 256, 256,
};

const uint8_t HpackParser::ParseContext::inverse_base64_[256] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 62,  255,
    255, 255, 63,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  255, 255,
    255, 64,  255, 255, 255, 0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
    10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,
    25,  255, 255, 255, 255, 255, 255, 26,  27,  28,  29,  30,  31,  32,  33,
    34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,
    49,  50,  51,  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255,
};

}  // namespace chttp2
}  // namespace grpc_core

#if 0
/* emission helpers */
static grpc_error* on_hdr(grpc_chttp2_hpack_parser* p, grpc_mdelem md,
                          int add_to_table) {
  if (grpc_http_trace.enabled()) {
    char* k = grpc_slice_to_c_string(GRPC_MDKEY(md));
    char* v = nullptr;
    if (grpc_is_binary_header(GRPC_MDKEY(md))) {
      v = grpc_dump_slice(GRPC_MDVALUE(md), GPR_DUMP_HEX);
    } else {
      v = grpc_slice_to_c_string(GRPC_MDVALUE(md));
    }
    gpr_log(
        GPR_DEBUG,
        "Decode: '%s: %s', elem_interned=%d [%d], k_interned=%d, v_interned=%d",
        k, v, GRPC_MDELEM_IS_INTERNED(md), GRPC_MDELEM_STORAGE(md),
        grpc_slice_is_interned(GRPC_MDKEY(md)),
        grpc_slice_is_interned(GRPC_MDVALUE(md)));
    gpr_free(k);
    gpr_free(v);
  }
  if (add_to_table) {
    GPR_ASSERT(GRPC_MDELEM_STORAGE(md) == GRPC_MDELEM_STORAGE_INTERNED ||
               GRPC_MDELEM_STORAGE(md) == GRPC_MDELEM_STORAGE_STATIC);
    grpc_error* err = grpc_chttp2_hptbl_add(&p->table, md);
    if (err != GRPC_ERROR_NONE) return err;
  }
  if (p->on_header == nullptr) {
    GRPC_MDELEM_UNREF(md);
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("on_header callback not set");
  }
  p->on_header(p->on_header_user_data, md);
  return GRPC_ERROR_NONE;
}
#endif

static grpc_slice take_string(grpc_chttp2_hpack_parser* p,
                              grpc_chttp2_hpack_parser_string* str,
                              bool intern) {
  grpc_slice s;
  if (!str->copied) {
    if (intern) {
      s = grpc_slice_intern(str->data.referenced);
      grpc_slice_unref_internal(str->data.referenced);
    } else {
      s = str->data.referenced;
    }
    str->copied = true;
    str->data.referenced = grpc_empty_slice();
  } else if (intern) {
    s = grpc_slice_intern(grpc_slice_from_static_buffer(
        str->data.copied.str, str->data.copied.length));
  } else {
    s = grpc_slice_from_copied_buffer(str->data.copied.str,
                                      str->data.copied.length);
  }
  str->data.copied.length = 0;
  return s;
}

/* PUBLIC INTERFACE */

void grpc_chttp2_hpack_parser_init(grpc_chttp2_hpack_parser* p) {
  p->on_header = nullptr;
  p->on_header_user_data = nullptr;
  p->state = parse_begin;
  p->key.data.referenced = grpc_empty_slice();
  p->key.data.copied.str = nullptr;
  p->key.data.copied.capacity = 0;
  p->key.data.copied.length = 0;
  p->value.data.referenced = grpc_empty_slice();
  p->value.data.copied.str = nullptr;
  p->value.data.copied.capacity = 0;
  p->value.data.copied.length = 0;
  p->dynamic_table_update_allowed = 2;
  p->last_error = GRPC_ERROR_NONE;
  grpc_chttp2_hptbl_init(&p->table);
}

void grpc_chttp2_hpack_parser_set_has_priority(grpc_chttp2_hpack_parser* p) {
  p->after_prioritization = p->state;
  p->state = parse_stream_dep0;
}

void grpc_chttp2_hpack_parser_destroy(grpc_chttp2_hpack_parser* p) {
  grpc_chttp2_hptbl_destroy(&p->table);
  GRPC_ERROR_UNREF(p->last_error);
  grpc_slice_unref_internal(p->key.data.referenced);
  grpc_slice_unref_internal(p->value.data.referenced);
  gpr_free(p->key.data.copied.str);
  gpr_free(p->value.data.copied.str);
}

typedef void (*maybe_complete_func_type)(grpc_chttp2_transport* t,
                                         grpc_chttp2_stream* s);
static const maybe_complete_func_type maybe_complete_funcs[] = {
    grpc_chttp2_maybe_complete_recv_initial_metadata,
    grpc_chttp2_maybe_complete_recv_trailing_metadata};

static void force_client_rst_stream(void* sp, grpc_error* error) {
  grpc_chttp2_stream* s = (grpc_chttp2_stream*)sp;
  grpc_chttp2_transport* t = s->t;
  if (!s->write_closed) {
    grpc_slice_buffer_add(
        &t->qbuf, grpc_chttp2_rst_stream_create(s->id, GRPC_HTTP2_NO_ERROR,
                                                &s->stats.outgoing));
    grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_FORCE_RST_STREAM);
    grpc_chttp2_mark_stream_closed(t, s, true, true, GRPC_ERROR_NONE);
  }
  GRPC_CHTTP2_STREAM_UNREF(s, "final_rst");
}

static void parse_stream_compression_md(grpc_chttp2_transport* t,
                                        grpc_chttp2_stream* s,
                                        grpc_metadata_batch* initial_metadata) {
  if (initial_metadata->idx.named.content_encoding == nullptr ||
      grpc_stream_compression_method_parse(
          GRPC_MDVALUE(initial_metadata->idx.named.content_encoding->md), false,
          &s->stream_decompression_method) == 0) {
    s->stream_decompression_method =
        GRPC_STREAM_COMPRESSION_IDENTITY_DECOMPRESS;
  }
}

grpc_error* grpc_chttp2_header_parser_parse(void* hpack_parser,
                                            grpc_chttp2_transport* t,
                                            grpc_chttp2_stream* s,
                                            grpc_slice slice, int is_last) {
  grpc_chttp2_hpack_parser* parser = (grpc_chttp2_hpack_parser*)hpack_parser;
  GPR_TIMER_BEGIN("grpc_chttp2_hpack_parser_parse", 0);
  if (s != nullptr) {
    s->stats.incoming.header_bytes += GRPC_SLICE_LENGTH(slice);
  }
  grpc_error* error = grpc_chttp2_hpack_parser_parse(parser, slice);
  if (error != GRPC_ERROR_NONE) {
    GPR_TIMER_END("grpc_chttp2_hpack_parser_parse", 0);
    return error;
  }
  if (is_last) {
    if (parser->is_boundary && parser->state != parse_begin) {
      GPR_TIMER_END("grpc_chttp2_hpack_parser_parse", 0);
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "end of header frame not aligned with a hpack record boundary");
    }
    /* need to check for null stream: this can occur if we receive an invalid
       stream id on a header */
    if (s != nullptr) {
      if (parser->is_boundary) {
        if (s->header_frames_received == GPR_ARRAY_SIZE(s->metadata_buffer)) {
          GPR_TIMER_END("grpc_chttp2_hpack_parser_parse", 0);
          return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "Too many trailer frames");
        }
        /* Process stream compression md element if it exists */
        if (s->header_frames_received ==
            0) { /* Only acts on initial metadata */
          parse_stream_compression_md(t, s, &s->metadata_buffer[0].batch);
        }
        s->published_metadata[s->header_frames_received] =
            GRPC_METADATA_PUBLISHED_FROM_WIRE;
        maybe_complete_funcs[s->header_frames_received](t, s);
        s->header_frames_received++;
      }
      if (parser->is_eof) {
        if (t->is_client && !s->write_closed) {
          /* server eof ==> complete closure; we may need to forcefully close
             the stream. Wait until the combiner lock is ready to be released
             however -- it might be that we receive a RST_STREAM following this
             and can avoid the extra write */
          GRPC_CHTTP2_STREAM_REF(s, "final_rst");
          GRPC_CLOSURE_SCHED(
              GRPC_CLOSURE_CREATE(force_client_rst_stream, s,
                                  grpc_combiner_finally_scheduler(t->combiner)),
              GRPC_ERROR_NONE);
        }
        grpc_chttp2_mark_stream_closed(t, s, true, false, GRPC_ERROR_NONE);
      }
    }
    parser->on_header = nullptr;
    parser->on_header_user_data = nullptr;
    parser->is_boundary = 0xde;
    parser->is_eof = 0xde;
    parser->dynamic_table_update_allowed = 2;
  }
  GPR_TIMER_END("grpc_chttp2_hpack_parser_parse", 0);
  return GRPC_ERROR_NONE;
}
