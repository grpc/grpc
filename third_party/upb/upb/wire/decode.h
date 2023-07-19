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

// upb_decode: parsing into a upb_Message using a upb_MiniTable.

#ifndef UPB_WIRE_DECODE_H_
#define UPB_WIRE_DECODE_H_

#include "upb/mem/arena.h"
#include "upb/message/message.h"
#include "upb/mini_table/extension_registry.h"

// Must be last.
#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  /* If set, strings will alias the input buffer instead of copying into the
   * arena. */
  kUpb_DecodeOption_AliasString = 1,

  /* If set, the parse will return failure if any message is missing any
   * required fields when the message data ends.  The parse will still continue,
   * and the failure will only be reported at the end.
   *
   * IMPORTANT CAVEATS:
   *
   * 1. This can throw a false positive failure if an incomplete message is seen
   *    on the wire but is later completed when the sub-message occurs again.
   *    For this reason, a second pass is required to verify a failure, to be
   *    truly robust.
   *
   * 2. This can return a false success if you are decoding into a message that
   *    already has some sub-message fields present.  If the sub-message does
   *    not occur in the binary payload, we will never visit it and discover the
   *    incomplete sub-message.  For this reason, this check is only useful for
   *    implemting ParseFromString() semantics.  For MergeFromString(), a
   *    post-parse validation step will always be necessary. */
  kUpb_DecodeOption_CheckRequired = 2,

  /* EXPERIMENTAL:
   *
   * If set, the parser will allow parsing of sub-message fields that were not
   * previously linked using upb_MiniTable_SetSubMessage().  The data will be
   * parsed into an internal "empty" message type that cannot be accessed
   * directly, but can be later promoted into the true message type if the
   * sub-message fields are linked at a later time.
   *
   * Users should set this option if they intend to perform dynamic tree shaking
   * and promoting using the interfaces in message/promote.h.  If this option is
   * enabled, it is important that the resulting messages are only accessed by
   * code that is aware of promotion rules:
   *
   * 1. Message pointers in upb_Message, upb_Array, and upb_Map are represented
   *    by a tagged pointer upb_TaggedMessagePointer.  The tag indicates whether
   *    the message uses the internal "empty" type.
   *
   * 2. Any code *reading* these message pointers must test whether the "empty"
   *    tag bit is set, using the interfaces in mini_table/types.h.  However
   *    writing of message pointers should always use plain upb_Message*, since
   *    users are not allowed to create "empty" messages.
   *
   * 3. It is always safe to test whether a field is present or test the array
   *    length; these interfaces will reflect that empty messages are present,
   *    even though their data cannot be accessed without promoting first.
   *
   * 4. If a message pointer is indeed tagged as empty, the message may not be
   *    accessed directly, only promoted through the interfaces in
   *    message/promote.h.
   *
   * 5. Tagged/empty messages may never be created by the user.  They may only
   *    be created by the parser or the message-copying logic in message/copy.h.
   */
  kUpb_DecodeOption_ExperimentalAllowUnlinked = 4,
};

UPB_INLINE uint32_t upb_DecodeOptions_MaxDepth(uint16_t depth) {
  return (uint32_t)depth << 16;
}

UPB_INLINE uint16_t upb_DecodeOptions_GetMaxDepth(uint32_t options) {
  return options >> 16;
}

// Enforce an upper bound on recursion depth.
UPB_INLINE int upb_Decode_LimitDepth(uint32_t decode_options, uint32_t limit) {
  uint32_t max_depth = upb_DecodeOptions_GetMaxDepth(decode_options);
  if (max_depth > limit) max_depth = limit;
  return upb_DecodeOptions_MaxDepth(max_depth) | (decode_options & 0xffff);
}

typedef enum {
  kUpb_DecodeStatus_Ok = 0,
  kUpb_DecodeStatus_Malformed = 1,    // Wire format was corrupt
  kUpb_DecodeStatus_OutOfMemory = 2,  // Arena alloc failed
  kUpb_DecodeStatus_BadUtf8 = 3,      // String field had bad UTF-8
  kUpb_DecodeStatus_MaxDepthExceeded =
      4,  // Exceeded upb_DecodeOptions_MaxDepth

  // kUpb_DecodeOption_CheckRequired failed (see above), but the parse otherwise
  // succeeded.
  kUpb_DecodeStatus_MissingRequired = 5,

  // Unlinked sub-message field was present, but
  // kUpb_DecodeOptions_ExperimentalAllowUnlinked was not specified in the list
  // of options.
  kUpb_DecodeStatus_UnlinkedSubMessage = 6,
} upb_DecodeStatus;

UPB_API upb_DecodeStatus upb_Decode(const char* buf, size_t size,
                                    upb_Message* msg, const upb_MiniTable* l,
                                    const upb_ExtensionRegistry* extreg,
                                    int options, upb_Arena* arena);

#ifdef __cplusplus
} /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif /* UPB_WIRE_DECODE_H_ */
