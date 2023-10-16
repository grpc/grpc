/*
 * Copyright (c) 2009-2022, Google LLC
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

#ifndef UPB_MINI_DESCRIPTOR_INTERNAL_ENCODE_H_
#define UPB_MINI_DESCRIPTOR_INTERNAL_ENCODE_H_

#include "upb/base/descriptor_constants.h"

// Must be last.
#include "upb/port/def.inc"

// If the input buffer has at least this many bytes available, the encoder call
// is guaranteed to succeed (as long as field number order is maintained).
#define kUpb_MtDataEncoder_MinSize 16

typedef struct {
  char* end;  // Limit of the buffer passed as a parameter.
  // Aliased to internal-only members in .cc.
  char internal[32];
} upb_MtDataEncoder;

#ifdef __cplusplus
extern "C" {
#endif

// Encodes field/oneof information for a given message.  The sequence of calls
// should look like:
//
//   upb_MtDataEncoder e;
//   char buf[256];
//   char* ptr = buf;
//   e.end = ptr + sizeof(buf);
//   unit64_t msg_mod = ...; // bitwise & of kUpb_MessageModifiers or zero
//   ptr = upb_MtDataEncoder_StartMessage(&e, ptr, msg_mod);
//   // Fields *must* be in field number order.
//   ptr = upb_MtDataEncoder_PutField(&e, ptr, ...);
//   ptr = upb_MtDataEncoder_PutField(&e, ptr, ...);
//   ptr = upb_MtDataEncoder_PutField(&e, ptr, ...);
//
//   // If oneofs are present.  Oneofs must be encoded after regular fields.
//   ptr = upb_MiniTable_StartOneof(&e, ptr)
//   ptr = upb_MiniTable_PutOneofField(&e, ptr, ...);
//   ptr = upb_MiniTable_PutOneofField(&e, ptr, ...);
//
//   ptr = upb_MiniTable_StartOneof(&e, ptr);
//   ptr = upb_MiniTable_PutOneofField(&e, ptr, ...);
//   ptr = upb_MiniTable_PutOneofField(&e, ptr, ...);
//
// Oneofs must be encoded after all regular fields.
char* upb_MtDataEncoder_StartMessage(upb_MtDataEncoder* e, char* ptr,
                                     uint64_t msg_mod);
char* upb_MtDataEncoder_PutField(upb_MtDataEncoder* e, char* ptr,
                                 upb_FieldType type, uint32_t field_num,
                                 uint64_t field_mod);
char* upb_MtDataEncoder_StartOneof(upb_MtDataEncoder* e, char* ptr);
char* upb_MtDataEncoder_PutOneofField(upb_MtDataEncoder* e, char* ptr,
                                      uint32_t field_num);

// Encodes the set of values for a given enum. The values must be given in
// order (after casting to uint32_t), and repeats are not allowed.
char* upb_MtDataEncoder_StartEnum(upb_MtDataEncoder* e, char* ptr);
char* upb_MtDataEncoder_PutEnumValue(upb_MtDataEncoder* e, char* ptr,
                                     uint32_t val);
char* upb_MtDataEncoder_EndEnum(upb_MtDataEncoder* e, char* ptr);

// Encodes an entire mini descriptor for an extension.
char* upb_MtDataEncoder_EncodeExtension(upb_MtDataEncoder* e, char* ptr,
                                        upb_FieldType type, uint32_t field_num,
                                        uint64_t field_mod);

// Encodes an entire mini descriptor for a map.
char* upb_MtDataEncoder_EncodeMap(upb_MtDataEncoder* e, char* ptr,
                                  upb_FieldType key_type,
                                  upb_FieldType value_type, uint64_t key_mod,
                                  uint64_t value_mod);

// Encodes an entire mini descriptor for a message set.
char* upb_MtDataEncoder_EncodeMessageSet(upb_MtDataEncoder* e, char* ptr);

#ifdef __cplusplus
} /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif /* UPB_MINI_DESCRIPTOR_INTERNAL_ENCODE_H_ */
