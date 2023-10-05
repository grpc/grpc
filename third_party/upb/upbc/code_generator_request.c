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

#include "upbc/code_generator_request.h"

#include <inttypes.h>

#include "google/protobuf/compiler/plugin.upb.h"
#include "upb/mini_descriptor/decode.h"
#include "upb/reflection/def.h"

// Must be last.
#include "upb/port/def.inc"

/******************************************************************************/

// Kitchen sink storage for all of our state as we build the mini descriptors.

typedef struct {
  upb_Arena* arena;
  upb_Status* status;
  upb_DefPool* symtab;

  upbc_CodeGeneratorRequest* out;

  jmp_buf jmp;
} upbc_State;

static void upbc_State_Fini(upbc_State* s) {
  if (s->symtab) upb_DefPool_Free(s->symtab);
}

UPB_NORETURN static void upbc_Error(upbc_State* s, const char* fn,
                                    const char* msg) {
  upb_Status_SetErrorFormat(s->status, "%s(): %s", fn, msg);
  upbc_State_Fini(s);
  UPB_LONGJMP(s->jmp, -1);
}

static void upbc_State_Init(upbc_State* s) {
  s->symtab = upb_DefPool_New();
  if (!s->symtab) upbc_Error(s, __func__, "could not allocate def pool");

  s->out = upbc_CodeGeneratorRequest_new(s->arena);
  if (!s->out) upbc_Error(s, __func__, "could not allocate request");
}

static upb_StringView upbc_State_StrDup(upbc_State* s, const char* str) {
  upb_StringView from = upb_StringView_FromString(str);
  char* to = upb_Arena_Malloc(s->arena, from.size);
  if (!to) upbc_Error(s, __func__, "Out of memory");
  memcpy(to, from.data, from.size);
  return upb_StringView_FromDataAndSize(to, from.size);
}

static void upbc_State_AddMiniDescriptor(upbc_State* s, const char* name,
                                         upb_StringView encoding) {
  const upb_StringView key = upb_StringView_FromString(name);
  upbc_CodeGeneratorRequest_UpbInfo* info =
      upbc_CodeGeneratorRequest_UpbInfo_new(s->arena);
  if (!info) upbc_Error(s, __func__, "Out of memory");
  upbc_CodeGeneratorRequest_UpbInfo_set_mini_descriptor(info, encoding);
  bool ok = upbc_CodeGeneratorRequest_upb_info_set(s->out, key, info, s->arena);
  if (!ok) upbc_Error(s, __func__, "could not set mini descriptor in map");
}

/******************************************************************************/

// Forward declaration.
static void upbc_Scrape_Message(upbc_State*, const upb_MessageDef*);

static void upbc_Scrape_Enum(upbc_State* s, const upb_EnumDef* e) {
  upb_StringView desc;
  bool ok = upb_EnumDef_MiniDescriptorEncode(e, s->arena, &desc);
  if (!ok) upbc_Error(s, __func__, "could not encode enum");

  upbc_State_AddMiniDescriptor(s, upb_EnumDef_FullName(e), desc);
}

static void upbc_Scrape_Extension(upbc_State* s, const upb_FieldDef* f) {
  upb_StringView desc;
  bool ok = upb_FieldDef_MiniDescriptorEncode(f, s->arena, &desc);
  if (!ok) upbc_Error(s, __func__, "could not encode extension");

  upbc_State_AddMiniDescriptor(s, upb_FieldDef_FullName(f), desc);
}

static void upbc_Scrape_FileEnums(upbc_State* s, const upb_FileDef* f) {
  const size_t len = upb_FileDef_TopLevelEnumCount(f);

  for (size_t i = 0; i < len; i++) {
    upbc_Scrape_Enum(s, upb_FileDef_TopLevelEnum(f, i));
  }
}

static void upbc_Scrape_FileExtensions(upbc_State* s, const upb_FileDef* f) {
  const size_t len = upb_FileDef_TopLevelExtensionCount(f);

  for (size_t i = 0; i < len; i++) {
    upbc_Scrape_Extension(s, upb_FileDef_TopLevelExtension(f, i));
  }
}

static void upbc_Scrape_FileMessages(upbc_State* s, const upb_FileDef* f) {
  const size_t len = upb_FileDef_TopLevelMessageCount(f);

  for (size_t i = 0; i < len; i++) {
    upbc_Scrape_Message(s, upb_FileDef_TopLevelMessage(f, i));
  }
}

static void upbc_Scrape_File(upbc_State* s, const upb_FileDef* f) {
  upbc_Scrape_FileEnums(s, f);
  upbc_Scrape_FileExtensions(s, f);
  upbc_Scrape_FileMessages(s, f);
}

static void upbc_Scrape_Files(upbc_State* s) {
  const google_protobuf_compiler_CodeGeneratorRequest* request =
      upbc_CodeGeneratorRequest_request(s->out);

  size_t len = 0;
  const google_protobuf_FileDescriptorProto* const* files =
      google_protobuf_compiler_CodeGeneratorRequest_proto_file(request, &len);

  for (size_t i = 0; i < len; i++) {
    const upb_FileDef* f = upb_DefPool_AddFile(s->symtab, files[i], s->status);
    if (!f) upbc_Error(s, __func__, "could not add file to def pool");

    upbc_Scrape_File(s, f);
  }
}

static void upbc_Scrape_NestedEnums(upbc_State* s, const upb_MessageDef* m) {
  const size_t len = upb_MessageDef_NestedEnumCount(m);

  for (size_t i = 0; i < len; i++) {
    upbc_Scrape_Enum(s, upb_MessageDef_NestedEnum(m, i));
  }
}

static void upbc_Scrape_NestedExtensions(upbc_State* s,
                                         const upb_MessageDef* m) {
  const size_t len = upb_MessageDef_NestedExtensionCount(m);

  for (size_t i = 0; i < len; i++) {
    upbc_Scrape_Extension(s, upb_MessageDef_NestedExtension(m, i));
  }
}

static void upbc_Scrape_NestedMessages(upbc_State* s, const upb_MessageDef* m) {
  const size_t len = upb_MessageDef_NestedMessageCount(m);

  for (size_t i = 0; i < len; i++) {
    upbc_Scrape_Message(s, upb_MessageDef_NestedMessage(m, i));
  }
}

static void upbc_Scrape_MessageSubs(upbc_State* s,
                                    upbc_CodeGeneratorRequest_UpbInfo* info,
                                    const upb_MessageDef* m) {
  const upb_MiniTableField** fields =
      malloc(upb_MessageDef_FieldCount(m) * sizeof(*fields));
  const upb_MiniTable* mt = upb_MessageDef_MiniTable(m);
  uint32_t counts = upb_MiniTable_GetSubList(mt, fields);
  uint32_t msg_count = counts >> 16;
  uint32_t enum_count = counts & 0xffff;

  for (uint32_t i = 0; i < msg_count; i++) {
    const upb_FieldDef* f =
        upb_MessageDef_FindFieldByNumber(m, fields[i]->number);
    if (!f) upbc_Error(s, __func__, "Missing f");
    const upb_MessageDef* sub = upb_FieldDef_MessageSubDef(f);
    if (!sub) upbc_Error(s, __func__, "Missing sub");
    upb_StringView name = upbc_State_StrDup(s, upb_MessageDef_FullName(sub));
    upbc_CodeGeneratorRequest_UpbInfo_add_sub_message(info, name, s->arena);
  }

  for (uint32_t i = 0; i < enum_count; i++) {
    const upb_FieldDef* f =
        upb_MessageDef_FindFieldByNumber(m, fields[msg_count + i]->number);
    if (!f) upbc_Error(s, __func__, "Missing f (2)");
    const upb_EnumDef* sub = upb_FieldDef_EnumSubDef(f);
    if (!sub) upbc_Error(s, __func__, "Missing sub (2)");
    upb_StringView name = upbc_State_StrDup(s, upb_EnumDef_FullName(sub));
    upbc_CodeGeneratorRequest_UpbInfo_add_sub_enum(info, name, s->arena);
  }

  free(fields);
}

static void upbc_Scrape_Message(upbc_State* s, const upb_MessageDef* m) {
  upb_StringView desc;
  bool ok = upb_MessageDef_MiniDescriptorEncode(m, s->arena, &desc);
  if (!ok) upbc_Error(s, __func__, "could not encode message");

  upbc_CodeGeneratorRequest_UpbInfo* info =
      upbc_CodeGeneratorRequest_UpbInfo_new(s->arena);
  if (!info) upbc_Error(s, __func__, "Out of memory");
  upbc_CodeGeneratorRequest_UpbInfo_set_mini_descriptor(info, desc);

  upbc_Scrape_MessageSubs(s, info, m);

  const upb_StringView key = upbc_State_StrDup(s, upb_MessageDef_FullName(m));
  ok = upbc_CodeGeneratorRequest_upb_info_set(s->out, key, info, s->arena);
  if (!ok) upbc_Error(s, __func__, "could not set mini descriptor in map");

  upbc_Scrape_NestedEnums(s, m);
  upbc_Scrape_NestedExtensions(s, m);
  upbc_Scrape_NestedMessages(s, m);
}

static upbc_CodeGeneratorRequest* upbc_State_MakeCodeGeneratorRequest(
    upbc_State* const s, google_protobuf_compiler_CodeGeneratorRequest* const request) {
  if (UPB_SETJMP(s->jmp)) return NULL;
  upbc_State_Init(s);

  upbc_CodeGeneratorRequest_set_request(s->out, request);
  upbc_Scrape_Files(s);
  upbc_State_Fini(s);
  return s->out;
}

upbc_CodeGeneratorRequest* upbc_MakeCodeGeneratorRequest(
    google_protobuf_compiler_CodeGeneratorRequest* request, upb_Arena* arena,
    upb_Status* status) {
  upbc_State s = {
      .arena = arena,
      .status = status,
      .symtab = NULL,
      .out = NULL,
  };

  return upbc_State_MakeCodeGeneratorRequest(&s, request);
}
