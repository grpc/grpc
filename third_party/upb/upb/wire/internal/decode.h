// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/*
 * Internal implementation details of the decoder that are shared between
 * decode.c and decode_fast.c.
 */

#ifndef UPB_WIRE_INTERNAL_DECODE_H_
#define UPB_WIRE_INTERNAL_DECODE_H_

#include "upb/mem/internal/arena.h"
#include "upb/message/internal/message.h"
#include "upb/wire/decode.h"
#include "upb/wire/eps_copy_input_stream.h"
#include "utf8_range.h"

// Must be last.
#include "upb/port/def.inc"

#define DECODE_NOGROUP (uint32_t) - 1

typedef struct upb_Decoder {
  upb_EpsCopyInputStream input;
  const upb_ExtensionRegistry* extreg;
  const char* unknown;       // Start of unknown data, preserve at buffer flip
  upb_Message* unknown_msg;  // Pointer to preserve data to
  int depth;                 // Tracks recursion depth to bound stack usage.
  uint32_t end_group;  // field number of END_GROUP tag, else DECODE_NOGROUP.
  uint16_t options;
  bool missing_required;
  upb_Arena arena;
  upb_DecodeStatus status;
  jmp_buf err;

#ifndef NDEBUG
  const char* debug_tagstart;
  const char* debug_valstart;
#endif
} upb_Decoder;

/* Error function that will abort decoding with longjmp(). We can't declare this
 * UPB_NORETURN, even though it is appropriate, because if we do then compilers
 * will "helpfully" refuse to tailcall to it
 * (see: https://stackoverflow.com/a/55657013), which will defeat a major goal
 * of our optimizations. That is also why we must declare it in a separate file,
 * otherwise the compiler will see that it calls longjmp() and deduce that it is
 * noreturn. */
const char* _upb_FastDecoder_ErrorJmp(upb_Decoder* d, int status);

extern const uint8_t upb_utf8_offsets[];

UPB_INLINE
bool _upb_Decoder_VerifyUtf8Inline(const char* ptr, int len) {
  const char* end = ptr + len;

  // Check 8 bytes at a time for any non-ASCII char.
  while (end - ptr >= 8) {
    uint64_t data;
    memcpy(&data, ptr, 8);
    if (data & 0x8080808080808080) goto non_ascii;
    ptr += 8;
  }

  // Check one byte at a time for non-ASCII.
  while (ptr < end) {
    if (*ptr & 0x80) goto non_ascii;
    ptr++;
  }

  return true;

non_ascii:
  return utf8_range2((const unsigned char*)ptr, end - ptr) == 0;
}

const char* _upb_Decoder_CheckRequired(upb_Decoder* d, const char* ptr,
                                       const upb_Message* msg,
                                       const upb_MiniTable* l);

/* x86-64 pointers always have the high 16 bits matching. So we can shift
 * left 8 and right 8 without loss of information. */
UPB_INLINE intptr_t decode_totable(const upb_MiniTable* tablep) {
  return ((intptr_t)tablep << 8) | tablep->table_mask;
}

UPB_INLINE const upb_MiniTable* decode_totablep(intptr_t table) {
  return (const upb_MiniTable*)(table >> 8);
}

const char* _upb_Decoder_IsDoneFallback(upb_EpsCopyInputStream* e,
                                        const char* ptr, int overrun);

UPB_INLINE bool _upb_Decoder_IsDone(upb_Decoder* d, const char** ptr) {
  return upb_EpsCopyInputStream_IsDoneWithCallback(
      &d->input, ptr, &_upb_Decoder_IsDoneFallback);
}

UPB_INLINE const char* _upb_Decoder_BufferFlipCallback(
    upb_EpsCopyInputStream* e, const char* old_end, const char* new_start) {
  upb_Decoder* d = (upb_Decoder*)e;
  if (!old_end) _upb_FastDecoder_ErrorJmp(d, kUpb_DecodeStatus_Malformed);

  if (d->unknown) {
    if (!_upb_Message_AddUnknown(d->unknown_msg, d->unknown,
                                 old_end - d->unknown, &d->arena)) {
      _upb_FastDecoder_ErrorJmp(d, kUpb_DecodeStatus_OutOfMemory);
    }
    d->unknown = new_start;
  }
  return new_start;
}

#if UPB_FASTTABLE
UPB_INLINE
const char* _upb_FastDecoder_TagDispatch(upb_Decoder* d, const char* ptr,
                                         upb_Message* msg, intptr_t table,
                                         uint64_t hasbits, uint64_t tag) {
  const upb_MiniTable* table_p = decode_totablep(table);
  uint8_t mask = table;
  uint64_t data;
  size_t idx = tag & mask;
  UPB_ASSUME((idx & 7) == 0);
  idx >>= 3;
  data = table_p->fasttable[idx].field_data ^ tag;
  UPB_MUSTTAIL return table_p->fasttable[idx].field_parser(d, ptr, msg, table,
                                                           hasbits, data);
}
#endif

UPB_INLINE uint32_t _upb_FastDecoder_LoadTag(const char* ptr) {
  uint16_t tag;
  memcpy(&tag, ptr, 2);
  return tag;
}

#include "upb/port/undef.inc"

#endif /* UPB_WIRE_INTERNAL_DECODE_H_ */
