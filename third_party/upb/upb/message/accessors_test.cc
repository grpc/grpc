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

/* Test of mini table accessors.
 *
 * Messages are created and mutated using generated code, and then
 * accessed through reflective APIs exposed through mini table accessors.
 */

#include "upb/message/accessors.h"

#include "gtest/gtest.h"
#include "google/protobuf/test_messages_proto2.upb.h"
#include "google/protobuf/test_messages_proto3.upb.h"
#include "upb/base/string_view.h"
#include "upb/collections/array.h"
#include "upb/mini_table/common.h"
#include "upb/mini_table/decode.h"
#include "upb/mini_table/encode_internal.hpp"
#include "upb/mini_table/field_internal.h"
#include "upb/test/test.upb.h"
#include "upb/upb.h"
#include "upb/wire/decode.h"

namespace {

// Proto2 test messages field numbers used for reflective access.
const uint32_t kFieldOptionalInt32 = 1;
const uint32_t kFieldOptionalUInt32 = 3;
const uint32_t kFieldOptionalBool = 13;
const uint32_t kFieldOptionalString = 14;
const uint32_t kFieldOptionalNestedMessage = 18;
const uint32_t kFieldOptionalRepeatedInt32 = 31;
const uint32_t kFieldOptionalNestedMessageA = 1;
const uint32_t kFieldOptionalOneOfUInt32 = 111;
const uint32_t kFieldOptionalOneOfString = 113;

const uint32_t kFieldProto3OptionalInt64 = 2;
const uint32_t kFieldProto3OptionalUInt64 = 4;

const char kTestStr1[] = "Hello1";
const char kTestStr2[] = "Hello2";
const int32_t kTestInt32 = 567;
const int32_t kTestUInt32 = 0xF1234567;
const uint64_t kTestUInt64 = 0xFEDCBAFF87654321;

const upb_MiniTableField* find_proto3_field(int field_number) {
  return upb_MiniTable_FindFieldByNumber(
      &protobuf_test_messages_proto3_TestAllTypesProto3_msg_init, field_number);
}

const upb_MiniTableField* find_proto2_field(int field_number) {
  return upb_MiniTable_FindFieldByNumber(
      &protobuf_test_messages_proto2_TestAllTypesProto2_msg_init, field_number);
}

TEST(GeneratedCode, HazzersProto2) {
  upb_Arena* arena = upb_Arena_New();
  protobuf_test_messages_proto2_TestAllTypesProto2* msg =
      protobuf_test_messages_proto2_TestAllTypesProto2_new(arena);

  // Scalar/Boolean.
  const upb_MiniTableField* optional_bool_field =
      find_proto2_field(kFieldOptionalBool);
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_bool_field));
  protobuf_test_messages_proto2_TestAllTypesProto2_set_optional_bool(msg, true);
  EXPECT_EQ(true, upb_Message_HasField(msg, optional_bool_field));
  upb_Message_ClearField(msg, optional_bool_field);
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_bool_field));
  EXPECT_EQ(
      false,
      protobuf_test_messages_proto2_TestAllTypesProto2_optional_bool(msg));

  // String.
  const upb_MiniTableField* optional_string_field =
      find_proto2_field(kFieldOptionalString);
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_string_field));
  protobuf_test_messages_proto2_TestAllTypesProto2_set_optional_string(
      msg, upb_StringView_FromString(kTestStr1));
  EXPECT_EQ(true, upb_Message_HasField(msg, optional_string_field));
  EXPECT_EQ(
      strlen(kTestStr1),
      protobuf_test_messages_proto2_TestAllTypesProto2_optional_string(msg)
          .size);
  upb_Message_ClearField(msg, optional_string_field);
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_string_field));
  EXPECT_EQ(
      0, protobuf_test_messages_proto2_TestAllTypesProto2_optional_string(msg)
             .size);

  // Message.
  const upb_MiniTableField* optional_message_field =
      find_proto2_field(kFieldOptionalNestedMessage);
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_message_field));
  protobuf_test_messages_proto2_TestAllTypesProto2_mutable_optional_nested_message(
      msg, arena);
  EXPECT_EQ(true, upb_Message_HasField(msg, optional_message_field));
  upb_Message_ClearField(msg, optional_message_field);
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_message_field));
  EXPECT_EQ(
      true,
      protobuf_test_messages_proto2_TestAllTypesProto2_optional_nested_message(
          msg) == nullptr);

  // One of.
  const upb_MiniTableField* optional_oneof_uint32_field =
      find_proto2_field(kFieldOptionalOneOfUInt32);
  const upb_MiniTableField* optional_oneof_string_field =
      find_proto2_field(kFieldOptionalOneOfString);

  EXPECT_EQ(false, upb_Message_HasField(msg, optional_oneof_uint32_field));
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_oneof_string_field));
  protobuf_test_messages_proto2_TestAllTypesProto2_set_oneof_uint32(msg, 123);
  EXPECT_EQ(true, upb_Message_HasField(msg, optional_oneof_uint32_field));
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_oneof_string_field));
  protobuf_test_messages_proto2_TestAllTypesProto2_set_oneof_string(
      msg, upb_StringView_FromString(kTestStr1));
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_oneof_uint32_field));
  EXPECT_EQ(true, upb_Message_HasField(msg, optional_oneof_string_field));
  upb_Message_ClearField(msg, optional_oneof_uint32_field);
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_oneof_uint32_field));
  EXPECT_EQ(true, upb_Message_HasField(msg, optional_oneof_string_field));
  upb_Message_ClearField(msg, optional_oneof_string_field);
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_oneof_uint32_field));
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_oneof_string_field));

  upb_Arena_Free(arena);
}

TEST(GeneratedCode, ScalarsProto2) {
  upb_Arena* arena = upb_Arena_New();
  protobuf_test_messages_proto2_TestAllTypesProto2* msg =
      protobuf_test_messages_proto2_TestAllTypesProto2_new(arena);

  const upb_MiniTableField* optional_int32_field =
      find_proto2_field(kFieldOptionalInt32);

  EXPECT_EQ(
      0, protobuf_test_messages_proto2_TestAllTypesProto2_optional_int32(msg));

  EXPECT_EQ(0, upb_Message_GetInt32(msg, optional_int32_field, 0));
  upb_Message_SetInt32(msg, optional_int32_field, kTestInt32, NULL);
  EXPECT_EQ(true, upb_Message_HasField(msg, optional_int32_field));
  EXPECT_EQ(kTestInt32, upb_Message_GetInt32(msg, optional_int32_field, 0));
  EXPECT_EQ(
      kTestInt32,
      protobuf_test_messages_proto2_TestAllTypesProto2_optional_int32(msg));

  const upb_MiniTableField* optional_uint32_field =
      find_proto2_field(kFieldOptionalUInt32);

  EXPECT_EQ(
      0, protobuf_test_messages_proto2_TestAllTypesProto2_optional_uint32(msg));
  EXPECT_EQ(0, upb_Message_GetUInt32(msg, optional_uint32_field, 0));
  upb_Message_SetUInt32(msg, optional_uint32_field, kTestUInt32, NULL);
  EXPECT_EQ(kTestUInt32, upb_Message_GetUInt32(msg, optional_uint32_field, 0));
  EXPECT_EQ(
      kTestUInt32,
      protobuf_test_messages_proto2_TestAllTypesProto2_optional_uint32(msg));

  upb_Arena_Free(arena);
}

TEST(GeneratedCode, ScalarProto3) {
  upb_Arena* arena = upb_Arena_New();
  protobuf_test_messages_proto3_TestAllTypesProto3* msg =
      protobuf_test_messages_proto3_TestAllTypesProto3_new(arena);

  const upb_MiniTableField* optional_int64_field =
      find_proto3_field(kFieldProto3OptionalInt64);
  const upb_MiniTableField* optional_uint64_field =
      find_proto3_field(kFieldProto3OptionalUInt64);

  EXPECT_EQ(
      0, protobuf_test_messages_proto3_TestAllTypesProto3_optional_int64(msg));
  upb_Message_SetInt64(msg, optional_int64_field, -1, NULL);
  EXPECT_EQ(
      -1, protobuf_test_messages_proto3_TestAllTypesProto3_optional_int64(msg));
  EXPECT_EQ(-1, upb_Message_GetInt64(msg, optional_int64_field, 0));

  EXPECT_EQ(
      0, protobuf_test_messages_proto3_TestAllTypesProto3_optional_uint64(msg));
  upb_Message_SetUInt64(msg, optional_uint64_field, kTestUInt64, NULL);
  EXPECT_EQ(
      kTestUInt64,
      protobuf_test_messages_proto3_TestAllTypesProto3_optional_uint64(msg));
  EXPECT_EQ(kTestUInt64, upb_Message_GetUInt64(msg, optional_uint64_field, 0));

  upb_Arena_Free(arena);
}

TEST(GeneratedCode, Strings) {
  upb_Arena* arena = upb_Arena_New();
  protobuf_test_messages_proto2_TestAllTypesProto2* msg =
      protobuf_test_messages_proto2_TestAllTypesProto2_new(arena);

  const upb_MiniTableField* optional_string_field =
      find_proto2_field(kFieldOptionalString);

  // Test default.
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_string_field));
  // Test read after write using C.
  protobuf_test_messages_proto2_TestAllTypesProto2_set_optional_string(
      msg, upb_StringView_FromString(kTestStr1));
  EXPECT_EQ(true, upb_Message_HasField(msg, optional_string_field));
  upb_StringView value = upb_Message_GetString(msg, optional_string_field,
                                               upb_StringView{NULL, 0});
  std::string read_value = std::string(value.data, value.size);
  EXPECT_EQ(kTestStr1, read_value);
  // Clear.
  upb_Message_ClearField(msg, optional_string_field);
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_string_field));
  EXPECT_EQ(
      false,
      protobuf_test_messages_proto2_TestAllTypesProto2_has_optional_string(
          msg));
  upb_Message_SetString(msg, optional_string_field,
                        upb_StringView_FromString(kTestStr2), NULL);
  EXPECT_EQ(true, upb_Message_HasField(msg, optional_string_field));
  EXPECT_EQ(
      true,
      protobuf_test_messages_proto2_TestAllTypesProto2_has_optional_string(
          msg));
  value = protobuf_test_messages_proto2_TestAllTypesProto2_optional_string(msg);
  read_value = std::string(value.data, value.size);
  EXPECT_EQ(kTestStr2, read_value);

  upb_Arena_Free(arena);
}

TEST(GeneratedCode, SubMessage) {
  upb_Arena* arena = upb_Arena_New();
  protobuf_test_messages_proto2_TestAllTypesProto2* msg =
      protobuf_test_messages_proto2_TestAllTypesProto2_new(arena);

  const upb_MiniTableField* optional_message_field =
      find_proto2_field(kFieldOptionalNestedMessage);

  const upb_Message* test_message =
      upb_Message_GetMessage(msg, optional_message_field, NULL);
  EXPECT_EQ(NULL, test_message);

  EXPECT_EQ(false, upb_Message_HasField(msg, optional_message_field));

  // Get mutable using C API.
  protobuf_test_messages_proto2_TestAllTypesProto2_NestedMessage* nested_message =
      protobuf_test_messages_proto2_TestAllTypesProto2_mutable_optional_nested_message(
          msg, arena);
  EXPECT_EQ(true, nested_message != nullptr);
  EXPECT_EQ(true, upb_Message_HasField(msg, optional_message_field));
  protobuf_test_messages_proto2_TestAllTypesProto2_NestedMessage_set_a(
      nested_message, 5);

  // Read back using mini table API.
  const upb_Message* sub_message =
      upb_Message_GetMessage(msg, optional_message_field, NULL);
  EXPECT_EQ(true, sub_message != NULL);

  const upb_MiniTableField* nested_message_a_field =
      upb_MiniTable_FindFieldByNumber(
          &protobuf_test_messages_proto2_TestAllTypesProto2_NestedMessage_msg_init,
          kFieldOptionalNestedMessageA);
  EXPECT_EQ(5, upb_Message_GetInt32(sub_message, nested_message_a_field, 0));

  upb_Message_ClearField(msg, optional_message_field);
  EXPECT_EQ(
      NULL,
      protobuf_test_messages_proto2_TestAllTypesProto2_optional_nested_message(
          msg));
  EXPECT_EQ(false, upb_Message_HasField(msg, optional_message_field));

  upb_Message* new_nested_message =
      protobuf_test_messages_proto2_TestAllTypesProto2_NestedMessage_new(arena);
  upb_Message_SetInt32(new_nested_message, nested_message_a_field, 123, NULL);
  upb_Message_SetMessage(
      msg, &protobuf_test_messages_proto2_TestAllTypesProto2_msg_init,
      optional_message_field, new_nested_message);

  upb_Message* mutable_message = upb_Message_GetOrCreateMutableMessage(
      msg, &protobuf_test_messages_proto2_TestAllTypesProto2_msg_init,
      optional_message_field, arena);
  EXPECT_EQ(
      true,
      protobuf_test_messages_proto2_TestAllTypesProto2_optional_nested_message(
          msg) != NULL);
  EXPECT_EQ(true, upb_Message_HasField(msg, optional_message_field));
  EXPECT_EQ(123,
            upb_Message_GetInt32(mutable_message, nested_message_a_field, 0));

  upb_Arena_Free(arena);
}

TEST(GeneratedCode, RepeatedScalar) {
  upb_Arena* arena = upb_Arena_New();
  protobuf_test_messages_proto2_TestAllTypesProto2* msg =
      protobuf_test_messages_proto2_TestAllTypesProto2_new(arena);

  const upb_MiniTableField* repeated_int32_field =
      find_proto2_field(kFieldOptionalRepeatedInt32);

  size_t len;
  const int32_t* arr =
      protobuf_test_messages_proto2_TestAllTypesProto2_repeated_int32(msg,
                                                                      &len);
  // Test Get/Set Array values, validate with C API.
  EXPECT_EQ(0, len);
  EXPECT_EQ(NULL, arr);
  EXPECT_EQ(NULL, upb_Message_GetArray(msg, repeated_int32_field));
  protobuf_test_messages_proto2_TestAllTypesProto2_resize_repeated_int32(
      msg, 10, arena);
  int32_t* mutable_values =
      protobuf_test_messages_proto2_TestAllTypesProto2_mutable_repeated_int32(
          msg, &len);
  mutable_values[5] = 123;
  const upb_Array* readonly_arr =
      upb_Message_GetArray(msg, repeated_int32_field);
  EXPECT_EQ(123, upb_Array_Get(readonly_arr, 5).int32_val);

  upb_MessageValue new_value;
  new_value.int32_val = 567;
  upb_Array* mutable_array =
      upb_Message_GetMutableArray(msg, repeated_int32_field);
  upb_Array_Set(mutable_array, 5, new_value);
  EXPECT_EQ(new_value.int32_val,
            protobuf_test_messages_proto2_TestAllTypesProto2_repeated_int32(
                msg, &len)[5]);

  // Test resize.
  bool result = upb_Array_Resize(mutable_array, 20, arena);
  EXPECT_EQ(true, result);
  upb_Array_Set(mutable_array, 19, new_value);
  EXPECT_EQ(new_value.int32_val,
            protobuf_test_messages_proto2_TestAllTypesProto2_repeated_int32(
                msg, &len)[19]);
  upb_Array_Resize(mutable_array, 0, arena);
  const int32_t* zero_length_array =
      protobuf_test_messages_proto2_TestAllTypesProto2_repeated_int32(msg,
                                                                      &len);
  EXPECT_EQ(0, len);
  EXPECT_EQ(true, zero_length_array != NULL);

  upb_Arena_Free(arena);
}

TEST(GeneratedCode, GetMutableMessage) {
  upb_Arena* arena = upb_Arena_New();
  protobuf_test_messages_proto2_TestAllTypesProto2* msg =
      protobuf_test_messages_proto2_TestAllTypesProto2_new(arena);
  // Message.
  const upb_MiniTableField* optional_message_field =
      find_proto2_field(kFieldOptionalNestedMessage);
  upb_Message* msg1 = upb_Message_GetOrCreateMutableMessage(
      msg, &protobuf_test_messages_proto2_TestAllTypesProto2_msg_init,
      optional_message_field, arena);
  upb_Message* msg2 = upb_Message_GetOrCreateMutableMessage(
      msg, &protobuf_test_messages_proto2_TestAllTypesProto2_msg_init,
      optional_message_field, arena);
  // Verify that newly constructed sub message is stored in msg.
  EXPECT_EQ(msg1, msg2);

  upb_Arena_Free(arena);
}

TEST(GeneratedCode, FindUnknown) {
  upb_Arena* arena = upb_Arena_New();
  upb_test_ModelWithExtensions* msg = upb_test_ModelWithExtensions_new(arena);
  upb_test_ModelWithExtensions_set_random_int32(msg, 10);
  upb_test_ModelWithExtensions_set_random_name(
      msg, upb_StringView_FromString("Hello"));

  upb_test_ModelExtension1* extension1 = upb_test_ModelExtension1_new(arena);
  upb_test_ModelExtension1_set_str(extension1,
                                   upb_StringView_FromString("World"));

  upb_test_ModelExtension1_set_model_ext(msg, extension1, arena);

  size_t serialized_size;
  char* serialized =
      upb_test_ModelWithExtensions_serialize(msg, arena, &serialized_size);

  upb_test_EmptyMessageWithExtensions* base_msg =
      upb_test_EmptyMessageWithExtensions_parse(serialized, serialized_size,
                                                arena);

  upb_FindUnknownRet result = upb_MiniTable_FindUnknown(
      base_msg, upb_test_ModelExtension1_model_ext_ext.field.number);
  EXPECT_EQ(kUpb_FindUnknown_Ok, result.status);

  result = upb_MiniTable_FindUnknown(
      base_msg, upb_test_ModelExtension2_model_ext_ext.field.number);
  EXPECT_EQ(kUpb_FindUnknown_NotPresent, result.status);

  upb_Arena_Free(arena);
}

TEST(GeneratedCode, Extensions) {
  upb_Arena* arena = upb_Arena_New();
  upb_test_ModelWithExtensions* msg = upb_test_ModelWithExtensions_new(arena);
  upb_test_ModelWithExtensions_set_random_int32(msg, 10);
  upb_test_ModelWithExtensions_set_random_name(
      msg, upb_StringView_FromString("Hello"));

  upb_test_ModelExtension1* extension1 = upb_test_ModelExtension1_new(arena);
  upb_test_ModelExtension1_set_str(extension1,
                                   upb_StringView_FromString("World"));

  upb_test_ModelExtension2* extension2 = upb_test_ModelExtension2_new(arena);
  upb_test_ModelExtension2_set_i(extension2, 5);

  upb_test_ModelExtension2* extension3 = upb_test_ModelExtension2_new(arena);
  upb_test_ModelExtension2_set_i(extension3, 6);

  upb_test_ModelExtension2* extension4 = upb_test_ModelExtension2_new(arena);
  upb_test_ModelExtension2_set_i(extension4, 7);

  upb_test_ModelExtension2* extension5 = upb_test_ModelExtension2_new(arena);
  upb_test_ModelExtension2_set_i(extension5, 8);

  upb_test_ModelExtension2* extension6 = upb_test_ModelExtension2_new(arena);
  upb_test_ModelExtension2_set_i(extension6, 9);

  // Set many extensions, to exercise code paths that involve reallocating the
  // extensions and unknown fields array.
  upb_test_ModelExtension1_set_model_ext(msg, extension1, arena);
  upb_test_ModelExtension2_set_model_ext(msg, extension2, arena);
  upb_test_ModelExtension2_set_model_ext_2(msg, extension3, arena);
  upb_test_ModelExtension2_set_model_ext_3(msg, extension4, arena);
  upb_test_ModelExtension2_set_model_ext_4(msg, extension5, arena);
  upb_test_ModelExtension2_set_model_ext_5(msg, extension6, arena);

  size_t serialized_size;
  char* serialized =
      upb_test_ModelWithExtensions_serialize(msg, arena, &serialized_size);

  const upb_Message_Extension* upb_ext2;
  upb_test_ModelExtension1* ext1;
  upb_test_ModelExtension2* ext2;
  upb_GetExtension_Status promote_status;

  // Test known GetExtension 1
  promote_status = upb_MiniTable_GetOrPromoteExtension(
      msg, &upb_test_ModelExtension1_model_ext_ext, 0, arena, &upb_ext2);
  ext1 = (upb_test_ModelExtension1*)upb_ext2->data.ptr;
  EXPECT_EQ(kUpb_GetExtension_Ok, promote_status);
  EXPECT_TRUE(upb_StringView_IsEqual(upb_StringView_FromString("World"),
                                     upb_test_ModelExtension1_str(ext1)));

  // Test known GetExtension 2
  promote_status = upb_MiniTable_GetOrPromoteExtension(
      msg, &upb_test_ModelExtension2_model_ext_ext, 0, arena, &upb_ext2);
  ext2 = (upb_test_ModelExtension2*)upb_ext2->data.ptr;
  EXPECT_EQ(kUpb_GetExtension_Ok, promote_status);
  EXPECT_EQ(5, upb_test_ModelExtension2_i(ext2));

  // Test known GetExtension 3
  promote_status = upb_MiniTable_GetOrPromoteExtension(
      msg, &upb_test_ModelExtension2_model_ext_2_ext, 0, arena, &upb_ext2);
  ext2 = (upb_test_ModelExtension2*)upb_ext2->data.ptr;
  EXPECT_EQ(kUpb_GetExtension_Ok, promote_status);
  EXPECT_EQ(6, upb_test_ModelExtension2_i(ext2));

  // Test known GetExtension 4
  promote_status = upb_MiniTable_GetOrPromoteExtension(
      msg, &upb_test_ModelExtension2_model_ext_3_ext, 0, arena, &upb_ext2);
  ext2 = (upb_test_ModelExtension2*)upb_ext2->data.ptr;
  EXPECT_EQ(kUpb_GetExtension_Ok, promote_status);
  EXPECT_EQ(7, upb_test_ModelExtension2_i(ext2));

  // Test known GetExtension 5
  promote_status = upb_MiniTable_GetOrPromoteExtension(
      msg, &upb_test_ModelExtension2_model_ext_4_ext, 0, arena, &upb_ext2);
  ext2 = (upb_test_ModelExtension2*)upb_ext2->data.ptr;
  EXPECT_EQ(kUpb_GetExtension_Ok, promote_status);
  EXPECT_EQ(8, upb_test_ModelExtension2_i(ext2));

  // Test known GetExtension 6
  promote_status = upb_MiniTable_GetOrPromoteExtension(
      msg, &upb_test_ModelExtension2_model_ext_5_ext, 0, arena, &upb_ext2);
  ext2 = (upb_test_ModelExtension2*)upb_ext2->data.ptr;
  EXPECT_EQ(kUpb_GetExtension_Ok, promote_status);
  EXPECT_EQ(9, upb_test_ModelExtension2_i(ext2));

  upb_test_EmptyMessageWithExtensions* base_msg =
      upb_test_EmptyMessageWithExtensions_parse(serialized, serialized_size,
                                                arena);

  // Get unknown extension bytes before promotion.
  const char* extension_data;
  size_t len;
  upb_GetExtensionAsBytes_Status status = upb_MiniTable_GetExtensionAsBytes(
      base_msg, &upb_test_ModelExtension2_model_ext_ext, 0, arena,
      &extension_data, &len);
  EXPECT_EQ(kUpb_GetExtensionAsBytes_Ok, status);
  EXPECT_EQ(0x48, extension_data[0]);
  EXPECT_EQ(5, extension_data[1]);

  // Test unknown GetExtension.
  promote_status = upb_MiniTable_GetOrPromoteExtension(
      base_msg, &upb_test_ModelExtension1_model_ext_ext, 0, arena, &upb_ext2);
  ext1 = (upb_test_ModelExtension1*)upb_ext2->data.ptr;
  EXPECT_EQ(kUpb_GetExtension_Ok, promote_status);
  EXPECT_TRUE(upb_StringView_IsEqual(upb_StringView_FromString("World"),
                                     upb_test_ModelExtension1_str(ext1)));

  // Test unknown GetExtension.
  promote_status = upb_MiniTable_GetOrPromoteExtension(
      base_msg, &upb_test_ModelExtension2_model_ext_ext, 0, arena, &upb_ext2);
  ext2 = (upb_test_ModelExtension2*)upb_ext2->data.ptr;
  EXPECT_EQ(kUpb_GetExtension_Ok, promote_status);
  EXPECT_EQ(5, upb_test_ModelExtension2_i(ext2));

  // Test unknown GetExtension.
  promote_status = upb_MiniTable_GetOrPromoteExtension(
      base_msg, &upb_test_ModelExtension2_model_ext_2_ext, 0, arena, &upb_ext2);
  ext2 = (upb_test_ModelExtension2*)upb_ext2->data.ptr;
  EXPECT_EQ(kUpb_GetExtension_Ok, promote_status);
  EXPECT_EQ(6, upb_test_ModelExtension2_i(ext2));

  // Test unknown GetExtension.
  promote_status = upb_MiniTable_GetOrPromoteExtension(
      base_msg, &upb_test_ModelExtension2_model_ext_3_ext, 0, arena, &upb_ext2);
  ext2 = (upb_test_ModelExtension2*)upb_ext2->data.ptr;
  EXPECT_EQ(kUpb_GetExtension_Ok, promote_status);
  EXPECT_EQ(7, upb_test_ModelExtension2_i(ext2));

  // Test unknown GetExtension.
  promote_status = upb_MiniTable_GetOrPromoteExtension(
      base_msg, &upb_test_ModelExtension2_model_ext_4_ext, 0, arena, &upb_ext2);
  ext2 = (upb_test_ModelExtension2*)upb_ext2->data.ptr;
  EXPECT_EQ(kUpb_GetExtension_Ok, promote_status);
  EXPECT_EQ(8, upb_test_ModelExtension2_i(ext2));

  upb_Arena_Free(arena);
}

// Create a minitable to mimic ModelWithSubMessages with unlinked subs
// to lazily promote unknowns after parsing.
upb_MiniTable* CreateMiniTableWithEmptySubTables(upb_Arena* arena) {
  upb::MtDataEncoder e;
  e.StartMessage(0);
  e.PutField(kUpb_FieldType_Int32, 4, 0);
  e.PutField(kUpb_FieldType_Message, 5, 0);
  e.PutField(kUpb_FieldType_Message, 6, kUpb_FieldModifier_IsRepeated);

  upb_Status status;
  upb_Status_Clear(&status);
  upb_MiniTable* table =
      upb_MiniTable_Build(e.data().data(), e.data().size(), arena, &status);
  EXPECT_EQ(status.ok, true);
  // Initialize sub table to null. Not using upb_MiniTable_SetSubMessage
  // since it checks ->ext on parameter.
  upb_MiniTableSub* sub = const_cast<upb_MiniTableSub*>(
      &table->subs[table->fields[1].submsg_index]);
  sub->submsg = nullptr;
  sub = const_cast<upb_MiniTableSub*>(
      &table->subs[table->fields[2].submsg_index]);
  sub->submsg = nullptr;
  return table;
}

// Create a minitable to mimic ModelWithMaps with unlinked subs
// to lazily promote unknowns after parsing.
upb_MiniTable* CreateMiniTableWithEmptySubTablesForMaps(upb_Arena* arena) {
  upb::MtDataEncoder e;
  e.StartMessage(0);
  e.PutField(kUpb_FieldType_Int32, 1, 0);
  e.PutField(kUpb_FieldType_Message, 3, kUpb_FieldModifier_IsRepeated);
  e.PutField(kUpb_FieldType_Message, 4, kUpb_FieldModifier_IsRepeated);

  upb_Status status;
  upb_Status_Clear(&status);
  upb_MiniTable* table =
      upb_MiniTable_Build(e.data().data(), e.data().size(), arena, &status);
  EXPECT_EQ(status.ok, true);
  // Initialize sub table to null. Not using upb_MiniTable_SetSubMessage
  // since it checks ->ext on parameter.
  upb_MiniTableSub* sub = const_cast<upb_MiniTableSub*>(
      &table->subs[table->fields[1].submsg_index]);
  sub->submsg = nullptr;
  sub = const_cast<upb_MiniTableSub*>(
      &table->subs[table->fields[2].submsg_index]);
  sub->submsg = nullptr;
  return table;
}

upb_MiniTable* CreateMapEntryMiniTable(upb_Arena* arena) {
  upb::MtDataEncoder e;
  e.EncodeMap(kUpb_FieldType_String, kUpb_FieldType_String, 0, 0);
  upb_Status status;
  upb_Status_Clear(&status);
  upb_MiniTable* table =
      upb_MiniTable_Build(e.data().data(), e.data().size(), arena, &status);
  EXPECT_EQ(status.ok, true);
  return table;
}

TEST(GeneratedCode, PromoteUnknownMessage) {
  upb_Arena* arena = upb_Arena_New();
  upb_test_ModelWithSubMessages* input_msg =
      upb_test_ModelWithSubMessages_new(arena);
  upb_test_ModelWithExtensions* sub_message =
      upb_test_ModelWithExtensions_new(arena);
  upb_test_ModelWithSubMessages_set_id(input_msg, 11);
  upb_test_ModelWithExtensions_set_random_int32(sub_message, 12);
  upb_test_ModelWithSubMessages_set_optional_child(input_msg, sub_message);
  size_t serialized_size;
  char* serialized = upb_test_ModelWithSubMessages_serialize(input_msg, arena,
                                                             &serialized_size);

  upb_MiniTable* mini_table = CreateMiniTableWithEmptySubTables(arena);
  upb_Message* msg = _upb_Message_New(mini_table, arena);
  upb_DecodeStatus decode_status = upb_Decode(serialized, serialized_size, msg,
                                              mini_table, nullptr, 0, arena);
  EXPECT_EQ(decode_status, kUpb_DecodeStatus_Ok);
  int32_t val = upb_Message_GetInt32(
      msg, upb_MiniTable_FindFieldByNumber(mini_table, 4), 0);
  EXPECT_EQ(val, 11);
  upb_FindUnknownRet unknown = upb_MiniTable_FindUnknown(msg, 5);
  EXPECT_EQ(unknown.status, kUpb_FindUnknown_Ok);
  // Update mini table and promote unknown to a message.
  EXPECT_TRUE(upb_MiniTable_SetSubMessage(
      mini_table, (upb_MiniTableField*)&mini_table->fields[1],
      &upb_test_ModelWithExtensions_msg_init));
  const int decode_options =
      UPB_DECODE_MAXDEPTH(100);  // UPB_DECODE_ALIAS disabled.
  upb_UnknownToMessageRet promote_result =
      upb_MiniTable_PromoteUnknownToMessage(
          msg, mini_table, &mini_table->fields[1],
          &upb_test_ModelWithExtensions_msg_init, decode_options, arena);
  EXPECT_EQ(promote_result.status, kUpb_UnknownToMessage_Ok);
  const upb_Message* promoted_message =
      upb_Message_GetMessage(msg, &mini_table->fields[1], NULL);
  EXPECT_EQ(upb_test_ModelWithExtensions_random_int32(
                (upb_test_ModelWithExtensions*)promoted_message),
            12);
  upb_Arena_Free(arena);
}

TEST(GeneratedCode, PromoteUnknownRepeatedMessage) {
  upb_Arena* arena = upb_Arena_New();
  upb_test_ModelWithSubMessages* input_msg =
      upb_test_ModelWithSubMessages_new(arena);
  upb_test_ModelWithSubMessages_set_id(input_msg, 123);

  // Add 2 repeated messages to input_msg.
  upb_test_ModelWithExtensions* item =
      upb_test_ModelWithSubMessages_add_items(input_msg, arena);
  upb_test_ModelWithExtensions_set_random_int32(item, 5);
  item = upb_test_ModelWithSubMessages_add_items(input_msg, arena);
  upb_test_ModelWithExtensions_set_random_int32(item, 6);

  size_t serialized_size;
  char* serialized = upb_test_ModelWithSubMessages_serialize(input_msg, arena,
                                                             &serialized_size);

  upb_MiniTable* mini_table = CreateMiniTableWithEmptySubTables(arena);
  upb_Message* msg = _upb_Message_New(mini_table, arena);
  upb_DecodeStatus decode_status = upb_Decode(serialized, serialized_size, msg,
                                              mini_table, nullptr, 0, arena);
  EXPECT_EQ(decode_status, kUpb_DecodeStatus_Ok);
  int32_t val = upb_Message_GetInt32(
      msg, upb_MiniTable_FindFieldByNumber(mini_table, 4), 0);
  EXPECT_EQ(val, 123);

  // Check that we have repeated field data in an unknown.
  upb_FindUnknownRet unknown = upb_MiniTable_FindUnknown(msg, 6);
  EXPECT_EQ(unknown.status, kUpb_FindUnknown_Ok);

  // Update mini table and promote unknown to a message.
  EXPECT_TRUE(upb_MiniTable_SetSubMessage(
      mini_table, (upb_MiniTableField*)&mini_table->fields[2],
      &upb_test_ModelWithExtensions_msg_init));
  const int decode_options =
      UPB_DECODE_MAXDEPTH(100);  // UPB_DECODE_ALIAS disabled.
  upb_UnknownToMessage_Status promote_result =
      upb_MiniTable_PromoteUnknownToMessageArray(
          msg, &mini_table->fields[2], &upb_test_ModelWithExtensions_msg_init,
          decode_options, arena);
  EXPECT_EQ(promote_result, kUpb_UnknownToMessage_Ok);

  upb_Array* array = upb_Message_GetMutableArray(msg, &mini_table->fields[2]);
  const upb_Message* promoted_message = upb_Array_Get(array, 0).msg_val;
  EXPECT_EQ(upb_test_ModelWithExtensions_random_int32(
                (upb_test_ModelWithExtensions*)promoted_message),
            5);
  promoted_message = upb_Array_Get(array, 1).msg_val;
  EXPECT_EQ(upb_test_ModelWithExtensions_random_int32(
                (upb_test_ModelWithExtensions*)promoted_message),
            6);
  upb_Arena_Free(arena);
}

TEST(GeneratedCode, PromoteUnknownToMap) {
  upb_Arena* arena = upb_Arena_New();
  upb_test_ModelWithMaps* input_msg = upb_test_ModelWithMaps_new(arena);
  upb_test_ModelWithMaps_set_id(input_msg, 123);

  // Add 2 map entries.
  upb_test_ModelWithMaps_map_ss_set(input_msg,
                                    upb_StringView_FromString("key1"),
                                    upb_StringView_FromString("value1"), arena);
  upb_test_ModelWithMaps_map_ss_set(input_msg,
                                    upb_StringView_FromString("key2"),
                                    upb_StringView_FromString("value2"), arena);

  size_t serialized_size;
  char* serialized =
      upb_test_ModelWithMaps_serialize(input_msg, arena, &serialized_size);

  upb_MiniTable* mini_table = CreateMiniTableWithEmptySubTablesForMaps(arena);
  upb_MiniTable* map_entry_mini_table = CreateMapEntryMiniTable(arena);
  upb_Message* msg = _upb_Message_New(mini_table, arena);
  upb_DecodeStatus decode_status = upb_Decode(serialized, serialized_size, msg,
                                              mini_table, nullptr, 0, arena);
  EXPECT_EQ(decode_status, kUpb_DecodeStatus_Ok);
  int32_t val = upb_Message_GetInt32(
      msg, upb_MiniTable_FindFieldByNumber(mini_table, 1), 0);
  EXPECT_EQ(val, 123);

  // Check that we have map data in an unknown.
  upb_FindUnknownRet unknown = upb_MiniTable_FindUnknown(msg, 3);
  EXPECT_EQ(unknown.status, kUpb_FindUnknown_Ok);

  // Update mini table and promote unknown to a message.
  EXPECT_TRUE(upb_MiniTable_SetSubMessage(
      mini_table, (upb_MiniTableField*)&mini_table->fields[1],
      map_entry_mini_table));
  const int decode_options =
      UPB_DECODE_MAXDEPTH(100);  // UPB_DECODE_ALIAS disabled.
  upb_UnknownToMessage_Status promote_result =
      upb_MiniTable_PromoteUnknownToMap(msg, mini_table, &mini_table->fields[1],
                                        decode_options, arena);
  EXPECT_EQ(promote_result, kUpb_UnknownToMessage_Ok);

  upb_Map* map = upb_Message_GetOrCreateMutableMap(
      msg, map_entry_mini_table, &mini_table->fields[1], arena);
  EXPECT_NE(map, nullptr);
  // Lookup in map.
  upb_MessageValue key;
  key.str_val = upb_StringView_FromString("key2");
  upb_MessageValue value;
  EXPECT_TRUE(upb_Map_Get(map, key, &value));
  EXPECT_EQ(0, strncmp(value.str_val.data, "value2", 5));
  upb_Arena_Free(arena);
}

TEST(GeneratedCode, EnumClosedCheck) {
  upb_Arena* arena = upb_Arena_New();

  upb::MtDataEncoder e;
  e.StartMessage(0);
  e.PutField(kUpb_FieldType_Int32, 4, 0);
  e.PutField(kUpb_FieldType_Enum, 5, 0);

  upb_Status status;
  upb_Status_Clear(&status);
  upb_MiniTable* table =
      upb_MiniTable_Build(e.data().data(), e.data().size(), arena, &status);

  const upb_MiniTableField* enumField = &table->fields[1];
  EXPECT_EQ(upb_MiniTableField_Type(enumField), kUpb_FieldType_Enum);
  EXPECT_FALSE(upb_MiniTableField_IsClosedEnum(enumField));

  upb::MtDataEncoder e2;
  e2.StartMessage(0);
  e2.PutField(kUpb_FieldType_Int32, 4, 0);
  e2.PutField(kUpb_FieldType_Enum, 6, kUpb_FieldModifier_IsClosedEnum);

  upb_Status_Clear(&status);
  table =
      upb_MiniTable_Build(e2.data().data(), e2.data().size(), arena, &status);

  const upb_MiniTableField* closedEnumField = &table->fields[1];
  EXPECT_EQ(upb_MiniTableField_Type(closedEnumField), kUpb_FieldType_Enum);
  EXPECT_TRUE(upb_MiniTableField_IsClosedEnum(closedEnumField));
  upb_Arena_Free(arena);
}

}  // namespace
