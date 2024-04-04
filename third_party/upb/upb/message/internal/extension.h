// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef UPB_MESSAGE_INTERNAL_EXTENSION_H_
#define UPB_MESSAGE_INTERNAL_EXTENSION_H_

#include "upb/base/descriptor_constants.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.h"
#include "upb/message/message.h"
#include "upb/mini_table/extension.h"

// Must be last.
#include "upb/port/def.inc"

// The internal representation of an extension is self-describing: it contains
// enough information that we can serialize it to binary format without needing
// to look it up in a upb_ExtensionRegistry.
//
// This representation allocates 16 bytes to data on 64-bit platforms.
// This is rather wasteful for scalars (in the extreme case of bool,
// it wastes 15 bytes). We accept this because we expect messages to be
// the most common extension type.
typedef struct {
  const upb_MiniTableExtension* ext;
  union {
    upb_StringView str;
    void* ptr;
    char scalar_data[8];
  } data;
} upb_Message_Extension;

#ifdef __cplusplus
extern "C" {
#endif

// Adds the given extension data to the given message.
// |ext| is copied into the message instance.
// This logically replaces any previously-added extension with this number.
upb_Message_Extension* _upb_Message_GetOrCreateExtension(
    upb_Message* msg, const upb_MiniTableExtension* ext, upb_Arena* arena);

// Returns an array of extensions for this message.
// Note: the array is ordered in reverse relative to the order of creation.
const upb_Message_Extension* _upb_Message_Getexts(const upb_Message* msg,
                                                  size_t* count);

// Returns an extension for the given field number, or NULL if no extension
// exists for this field number.
const upb_Message_Extension* _upb_Message_Getext(
    const upb_Message* msg, const upb_MiniTableExtension* ext);

#ifdef __cplusplus
} /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif /* UPB_MESSAGE_INTERNAL_EXTENSION_H_ */
