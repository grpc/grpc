/*
 * Copyright (c) 2009-2021, Google LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Google LLC nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL Google LLC BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Internal implementation details of the decoder that are shared between
 * decode.c and decode_fast.c.
 */

#ifndef UPB_DECODE_INT_H_
#define UPB_DECODE_INT_H_

#include <setjmp.h>

#include "third_party/utf8_range/utf8_range.h"
#include "upb/decode.h"
#include "upb/msg_internal.h"
#include "upb/upb_internal.h"

/* Must be last. */
#include "upb/port_def.inc"

#define DECODE_NOGROUP (uint32_t) - 1

typedef struct upb_Decoder {
  const char* end;          /* Can read up to 16 bytes slop beyond this. */
  const char* limit_ptr;    /* = end + UPB_MIN(limit, 0) */
  upb_Message* unknown_msg; /* If non-NULL, add unknown data at buffer flip. */
  const char* unknown;      /* Start of unknown data. */
  const upb_ExtensionRegistry*
      extreg;         /* For looking up extensions during the parse. */
  int limit;          /* Submessage limit relative to end. */
  int depth;          /* Tracks recursion depth to bound stack usage. */
  uint32_t end_group; /* field number of END_GROUP tag, else DECODE_NOGROUP */
  uint16_t options;
  bool missing_required;
  char patch[32];
  upb_Arena arena;
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
const char* fastdecode_err(upb_Decoder* d, int status);

extern const uint8_t upb_utf8_offsets[];

UPB_INLINE
bool decode_verifyutf8_inl(const char* ptr, int len) {
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

const char* decode_checkrequired(upb_Decoder* d, const char* ptr,
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

UPB_INLINE
const char* decode_isdonefallback_inl(upb_Decoder* d, const char* ptr,
                                      int overrun, int* status) {
  if (overrun < d->limit) {
    /* Need to copy remaining data into patch buffer. */
    UPB_ASSERT(overrun < 16);
    if (d->unknown_msg) {
      if (!_upb_Message_AddUnknown(d->unknown_msg, d->unknown, ptr - d->unknown,
                                   &d->arena)) {
        *status = kUpb_DecodeStatus_OutOfMemory;
        return NULL;
      }
      d->unknown = &d->patch[0] + overrun;
    }
    memset(d->patch + 16, 0, 16);
    memcpy(d->patch, d->end, 16);
    ptr = &d->patch[0] + overrun;
    d->end = &d->patch[16];
    d->limit -= 16;
    d->limit_ptr = d->end + d->limit;
    d->options &= ~kUpb_DecodeOption_AliasString;
    UPB_ASSERT(ptr < d->limit_ptr);
    return ptr;
  } else {
    *status = kUpb_DecodeStatus_Malformed;
    return NULL;
  }
}

const char* decode_isdonefallback(upb_Decoder* d, const char* ptr, int overrun);

UPB_INLINE
bool decode_isdone(upb_Decoder* d, const char** ptr) {
  int overrun = *ptr - d->end;
  if (UPB_LIKELY(*ptr < d->limit_ptr)) {
    return false;
  } else if (UPB_LIKELY(overrun == d->limit)) {
    return true;
  } else {
    *ptr = decode_isdonefallback(d, *ptr, overrun);
    return false;
  }
}

#if UPB_FASTTABLE
UPB_INLINE
const char* fastdecode_tagdispatch(upb_Decoder* d, const char* ptr,
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

UPB_INLINE uint32_t fastdecode_loadtag(const char* ptr) {
  uint16_t tag;
  memcpy(&tag, ptr, 2);
  return tag;
}

UPB_INLINE void decode_checklimit(upb_Decoder* d) {
  UPB_ASSERT(d->limit_ptr == d->end + UPB_MIN(0, d->limit));
}

UPB_INLINE int decode_pushlimit(upb_Decoder* d, const char* ptr, int size) {
  int limit = size + (int)(ptr - d->end);
  int delta = d->limit - limit;
  decode_checklimit(d);
  d->limit = limit;
  d->limit_ptr = d->end + UPB_MIN(0, limit);
  decode_checklimit(d);
  return delta;
}

UPB_INLINE void decode_poplimit(upb_Decoder* d, const char* ptr,
                                int saved_delta) {
  UPB_ASSERT(ptr - d->end == d->limit);
  decode_checklimit(d);
  d->limit += saved_delta;
  d->limit_ptr = d->end + UPB_MIN(0, d->limit);
  decode_checklimit(d);
}

#include "upb/port_undef.inc"

#endif /* UPB_DECODE_INT_H_ */
