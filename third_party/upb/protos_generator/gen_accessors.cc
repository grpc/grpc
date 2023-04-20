// Copyright (c) 2009-2021, Google LLC
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of Google LLC nor the
//       names of its contributors may be used to endorse or promote products
//       derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL Google LLC BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "protos_generator/gen_accessors.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "protos_generator/gen_utils.h"
#include "protos_generator/output.h"
#include "upbc/keywords.h"
#include "upbc/names.h"

namespace protos_generator {

namespace protobuf = ::google::protobuf;

using NameToFieldDescriptorMap =
    absl::flat_hash_map<absl::string_view, const protobuf::FieldDescriptor*>;

void WriteFieldAccessorHazzer(const protobuf::Descriptor* desc,
                              const protobuf::FieldDescriptor* field,
                              const absl::string_view resolved_field_name,
                              const absl::string_view resolved_upbc_name,
                              Output& output);
void WriteFieldAccessorClear(const protobuf::Descriptor* desc,
                             const protobuf::FieldDescriptor* field,
                             const absl::string_view resolved_field_name,
                             const absl::string_view resolved_upbc_name,
                             Output& output);
void WriteMapFieldAccessors(const protobuf::Descriptor* desc,
                            const protobuf::FieldDescriptor* field,
                            const absl::string_view resolved_field_name,
                            const absl::string_view resolved_upbc_name,
                            Output& output);

void WriteMapAccessorDefinitions(const protobuf::Descriptor* message,
                                 const protobuf::FieldDescriptor* field,
                                 const absl::string_view resolved_field_name,
                                 const absl::string_view class_name,
                                 Output& output);
void WriteRepeatedMessageAccessor(const protobuf::Descriptor* message,
                                  const protobuf::FieldDescriptor* field,
                                  const absl::string_view resolved_field_name,
                                  absl::string_view class_name, Output& output);
void WriteRepeatedStringAccessor(const protobuf::Descriptor* message,
                                 const protobuf::FieldDescriptor* field,
                                 const absl::string_view resolved_field_name,
                                 const absl::string_view class_name,
                                 Output& output);
void WriteRepeatedScalarAccessor(const protobuf::Descriptor* message,
                                 const protobuf::FieldDescriptor* field,
                                 const absl::string_view resolved_field_name,
                                 absl::string_view class_name, Output& output);

// Returns C++ class member name by resolving naming conflicts across
// proto field names (such as clear_ prefixes) and keyword collisions.
//
// The Upb C generator prefixes all accessors with package and class names
// avoiding collisions. Therefore we need to use raw field names when calling
// into C accessors but need to fully resolve conflicts for C++ class members.
std::string ResolveFieldName(const protobuf::FieldDescriptor* field,
                             const NameToFieldDescriptorMap& field_names);

NameToFieldDescriptorMap CreateFieldNameMap(
    const protobuf::Descriptor* message) {
  NameToFieldDescriptorMap field_names;
  for (int i = 0; i < message->field_count(); i++) {
    const protobuf::FieldDescriptor* field = message->field(i);
    field_names.emplace(field->name(), field);
  }
  return field_names;
}

void WriteFieldAccessorsInHeader(const protobuf::Descriptor* desc,
                                 Output& output) {
  // Generate const methods.
  OutputIndenter i(output);

  auto field_names = CreateFieldNameMap(desc);
  auto upbc_field_names = upbc::CreateFieldNameMap(desc);

  for (const auto* field : FieldNumberOrder(desc)) {
    std::string resolved_field_name = ResolveFieldName(field, field_names);
    std::string resolved_upbc_name =
        upbc::ResolveFieldName(field, upbc_field_names);
    WriteFieldAccessorHazzer(desc, field, resolved_field_name,
                             resolved_upbc_name, output);
    WriteFieldAccessorClear(desc, field, resolved_field_name,
                            resolved_upbc_name, output);

    if (field->is_map()) {
      WriteMapFieldAccessors(desc, field, resolved_field_name,
                             resolved_upbc_name, output);
    } else if (desc->options().map_entry()) {
      // TODO(b/237399867) Implement map entry
    } else if (field->is_repeated()) {
      output(
          R"cc(
            inline size_t $1_size() const {
              size_t len;
              $0_$2(msg_, &len);
              return len;
            }

            inline void clear_$1() { $0_clear_$2(msg_); }
          )cc",
          MessageName(desc), resolved_field_name, resolved_upbc_name);

      if (field->cpp_type() == protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
        output(
            R"cc(
              $1 $2(size_t index) const;
              absl::StatusOr<$0> add_$2();
              $0 mutable_$2(size_t index) const;
            )cc",
            MessagePtrConstType(field, /* const */ false),
            MessagePtrConstType(field, /* const */ true), resolved_field_name,
            resolved_upbc_name);
      } else {
        output(
            R"cc(
              $0 $1(size_t index) const;
              bool add_$1($0 val);
              void set_$1(size_t index, $0 val);
              bool resize_$1(size_t len);
            )cc",
            CppConstType(field), resolved_field_name);
      }
    } else {
      // non-repeated.
      if (field->cpp_type() == protobuf::FieldDescriptor::CPPTYPE_STRING) {
        output(R"cc(
                 $0 $1() const;
                 void set_$1($0 value);
               )cc",
               CppConstType(field), resolved_field_name);
      } else if (field->cpp_type() ==
                 protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
        output(R"cc(
                 $1 $2() const;
                 $0 mutable_$2();
               )cc",
               MessagePtrConstType(field, /* const */ false),
               MessagePtrConstType(field, /* const */ true),
               resolved_field_name, resolved_upbc_name);
      } else {
        output(
            R"cc(
              inline $0 $1() const { return $2_$3(msg_); }
              inline void set_$1($0 value) { return $2_set_$3(msg_, value); }
            )cc",
            CppConstType(field), resolved_field_name, MessageName(desc),
            resolved_upbc_name);
      }
    }
  }
}

void WriteFieldAccessorHazzer(const protobuf::Descriptor* desc,
                              const protobuf::FieldDescriptor* field,
                              const absl::string_view resolved_field_name,
                              const absl::string_view resolved_upbc_name,
                              Output& output) {
  // Generate hazzer (if any).
  if (field->has_presence()) {
    // Has presence.
    output("inline bool has_$0() const { return $1_has_$2(msg_); }\n",
           resolved_field_name, MessageName(desc), resolved_upbc_name);
  }
}

void WriteFieldAccessorClear(const protobuf::Descriptor* desc,
                             const protobuf::FieldDescriptor* field,
                             const absl::string_view resolved_field_name,
                             const absl::string_view resolved_upbc_name,
                             Output& output) {
  if (field->has_presence()) {
    output("void clear_$0() { $2_clear_$1(msg_); }\n", resolved_field_name,
           resolved_upbc_name, MessageName(desc));
  }
}

void WriteMapFieldAccessors(const protobuf::Descriptor* desc,
                            const protobuf::FieldDescriptor* field,
                            const absl::string_view resolved_field_name,
                            const absl::string_view resolved_upbc_name,
                            Output& output) {
  const protobuf::Descriptor* entry = field->message_type();
  const protobuf::FieldDescriptor* key = entry->FindFieldByNumber(1);
  const protobuf::FieldDescriptor* val = entry->FindFieldByNumber(2);
  output(
      R"cc(
        inline size_t $0_size() const { return $1_$3_size(msg_); }
        inline void clear_$0() { $1_clear_$3(msg_); }
        void delete_$0($2 key);
      )cc",
      resolved_field_name, MessageName(desc), CppConstType(key),
      resolved_upbc_name);

  if (val->cpp_type() == protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
    output(
        R"cc(
          bool set_$0($1 key, $3 value);
          bool set_$0($1 key, $4 value);
          absl::StatusOr<$3> get_$0($1 key);
        )cc",
        resolved_field_name, CppConstType(key), CppConstType(val),
        MessagePtrConstType(val, /* is_const */ true),
        MessagePtrConstType(val, /* is_const */ false));
  } else {
    output(
        R"cc(
          bool set_$0($1 key, $2 value);
          absl::StatusOr<$2> get_$0($1 key);
        )cc",
        resolved_field_name, CppConstType(key), CppConstType(val));
  }
}

void WriteAccessorsInSource(const protobuf::Descriptor* desc, Output& output) {
  std::string class_name = ClassName(desc);
  absl::StrAppend(&class_name, "Access");
  output("namespace internal {\n");
  const char arena_expression[] = "arena_";
  auto field_names = CreateFieldNameMap(desc);
  auto upbc_field_names = upbc::CreateFieldNameMap(desc);

  // Generate const methods.
  OutputIndenter i(output);
  for (const auto* field : FieldNumberOrder(desc)) {
    std::string resolved_field_name = ResolveFieldName(field, field_names);
    std::string resolved_upbc_name =
        upbc::ResolveFieldName(field, upbc_field_names);
    if (field->is_map()) {
      WriteMapAccessorDefinitions(desc, field, resolved_field_name, class_name,
                                  output);
    } else if (desc->options().map_entry()) {
      // TODO(b/237399867) Implement map entry
    } else if (field->is_repeated()) {
      if (field->cpp_type() == protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
        WriteRepeatedMessageAccessor(desc, field, resolved_field_name,
                                     class_name, output);
      } else if (field->cpp_type() ==
                 protobuf::FieldDescriptor::CPPTYPE_STRING) {
        WriteRepeatedStringAccessor(desc, field, resolved_field_name,
                                    class_name, output);
      } else {
        WriteRepeatedScalarAccessor(desc, field, resolved_field_name,
                                    class_name, output);
      }
    } else {
      // non-repeated field.
      if (field->cpp_type() == protobuf::FieldDescriptor::CPPTYPE_STRING) {
        output(
            R"cc(
              $1 $0::$2() const {
                return ::protos::UpbStrToStringView($3_$4(msg_));
              }
            )cc",
            class_name, CppConstType(field), resolved_field_name,
            MessageName(desc), resolved_upbc_name);
        // Set string.
        output(
            R"cc(
              void $0::set_$2($1 value) {
                $4_set_$3(msg_, ::protos::UpbStrFromStringView(value, $5));
              }
            )cc",
            class_name, CppConstType(field), resolved_field_name,
            resolved_upbc_name, MessageName(desc), arena_expression);
      } else if (field->cpp_type() ==
                 protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
        output(
            R"cc(
              $1 $0::$2() const {
                if (!has_$2()) {
                  return $4::default_instance();
                }
                return ::protos::internal::CreateMessage<$4>((upb_Message*)($3_$5(msg_)));
              }
            )cc",
            class_name, MessagePtrConstType(field, /* is_const */ true),
            resolved_field_name, MessageName(desc),
            MessageBaseType(field, /* maybe_const */ false),
            resolved_upbc_name);

        output(
            R"cc(
              $1 $0::mutable_$2() {
                return ::protos::internal::CreateMessageProxy<$4>(
                    (upb_Message*)($3_mutable_$5(msg_, $6)), $6);
              }
            )cc",
            class_name, MessagePtrConstType(field, /* is_const */ false),
            resolved_field_name, MessageName(desc),
            MessageBaseType(field, /* maybe_const */ false), resolved_upbc_name,
            arena_expression);
      }
    }
  }
  output("\n");
  output("}  // namespace internal\n\n");
}

void WriteRepeatedMessageAccessor(const protobuf::Descriptor* message,
                                  const protobuf::FieldDescriptor* field,
                                  const absl::string_view resolved_field_name,
                                  const absl::string_view class_name,
                                  Output& output) {
  const char arena_expression[] = "arena_";
  absl::string_view upbc_name = field->name();
  output(
      R"cc(
        $1 $0::$2(size_t index) const {
          size_t len;
          auto* ptr = $3_$5(msg_, &len);
          assert(index < len);
          return ::protos::internal::CreateMessage<$4>((upb_Message*)*(ptr + index));
        }
      )cc",
      class_name, MessagePtrConstType(field, /* is_const */ true),
      resolved_field_name, MessageName(message),
      MessageBaseType(field, /* maybe_const */ false), upbc_name);
  output(
      R"cc(
        absl::StatusOr<$1> $0::add_$2() {
          auto new_msg = $3_add_$6(msg_, $5);
          if (!new_msg) {
            return ::protos::MessageAllocationError();
          }
          return ::protos::internal::CreateMessageProxy<$4>((upb_Message*)new_msg, $5);
        }
      )cc",
      class_name, MessagePtrConstType(field, /* const */ false),
      resolved_field_name, MessageName(message),
      MessageBaseType(field, /* maybe_const */ false), arena_expression,
      upbc_name);
  output(
      R"cc(
        $1 $0::mutable_$2(size_t index) const {
          size_t len;
          auto* ptr = $3_$6(msg_, &len);
          assert(index < len);
          return ::protos::internal::CreateMessageProxy<$4>(
              (upb_Message*)*(ptr + index), $5);
        }
      )cc",
      class_name, MessagePtrConstType(field, /* is_const */ false),
      resolved_field_name, MessageName(message),
      MessageBaseType(field, /* maybe_const */ false), arena_expression,
      upbc_name);
}

void WriteRepeatedStringAccessor(const protobuf::Descriptor* message,
                                 const protobuf::FieldDescriptor* field,
                                 const absl::string_view resolved_field_name,
                                 const absl::string_view class_name,
                                 Output& output) {
  absl::string_view upbc_name = field->name();
  output(
      R"cc(
        $1 $0::$2(size_t index) const {
          size_t len;
          auto* ptr = $3_mutable_$4(msg_, &len);
          assert(index < len);
          return ::protos::UpbStrToStringView(*(ptr + index));
        }
      )cc",
      class_name, CppConstType(field), resolved_field_name,
      MessageName(message), upbc_name);
  output(
      R"cc(
        bool $0::resize_$1(size_t len) {
          return $2_resize_$3(msg_, len, arena_);
        }
      )cc",
      class_name, resolved_field_name, MessageName(message), upbc_name);
  output(
      R"cc(
        bool $0::add_$2($1 val) {
          return $3_add_$4(msg_, ::protos::UpbStrFromStringView(val, arena_), arena_);
        }
      )cc",
      class_name, CppConstType(field), resolved_field_name,
      MessageName(message), upbc_name);
  output(
      R"cc(
        void $0::set_$2(size_t index, $1 val) {
          size_t len;
          auto* ptr = $3_mutable_$4(msg_, &len);
          assert(index < len);
          *(ptr + index) = ::protos::UpbStrFromStringView(val, arena_);
        }
      )cc",
      class_name, CppConstType(field), resolved_field_name,
      MessageName(message), upbc_name);
}

void WriteRepeatedScalarAccessor(const protobuf::Descriptor* message,
                                 const protobuf::FieldDescriptor* field,
                                 const absl::string_view resolved_field_name,
                                 const absl::string_view class_name,
                                 Output& output) {
  absl::string_view upbc_name = field->name();
  output(
      R"cc(
        $1 $0::$2(size_t index) const {
          size_t len;
          auto* ptr = $3_mutable_$4(msg_, &len);
          assert(index < len);
          return *(ptr + index);
        }
      )cc",
      class_name, CppConstType(field), resolved_field_name,
      MessageName(message), upbc_name);
  output(
      R"cc(
        bool $0::resize_$1(size_t len) {
          return $2_resize_$3(msg_, len, arena_);
        }
      )cc",
      class_name, resolved_field_name, MessageName(message), upbc_name);
  output(
      R"cc(
        bool $0::add_$2($1 val) { return $3_add_$4(msg_, val, arena_); }
      )cc",
      class_name, CppConstType(field), resolved_field_name,
      MessageName(message), upbc_name);
  output(
      R"cc(
        void $0::set_$2(size_t index, $1 val) {
          size_t len;
          auto* ptr = $3_mutable_$4(msg_, &len);
          assert(index < len);
          *(ptr + index) = val;
        }
      )cc",
      class_name, CppConstType(field), resolved_field_name,
      MessageName(message), upbc_name);
}

void WriteMapAccessorDefinitions(const protobuf::Descriptor* message,
                                 const protobuf::FieldDescriptor* field,
                                 const absl::string_view resolved_field_name,
                                 const absl::string_view class_name,
                                 Output& output) {
  const protobuf::Descriptor* entry = field->message_type();
  const protobuf::FieldDescriptor* key = entry->FindFieldByNumber(1);
  const protobuf::FieldDescriptor* val = entry->FindFieldByNumber(2);
  absl::string_view upbc_name = field->name();
  absl::string_view converted_key_name = "key";
  absl::string_view optional_conversion_code = "";

  if (key->cpp_type() == protobuf::FieldDescriptor::CPPTYPE_STRING) {
    // Insert conversion from absl::string_view to upb_StringView.
    // Creates upb_StringView on stack to prevent allocation.
    converted_key_name = "upb_key";
    optional_conversion_code =
        "upb_StringView upb_key = {key.data(), key.size()};\n";
  }
  if (val->cpp_type() == protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
    output(
        R"cc(
          bool $0::set_$1($2 key, $3 value) {
            $6return $4_$8_set(msg_, $7, ($5*)value->msg(), arena_);
          }
        )cc",
        class_name, resolved_field_name, CppConstType(key),
        MessagePtrConstType(val, /* is_const */ true), MessageName(message),
        MessageName(val->message_type()), optional_conversion_code,
        converted_key_name, upbc_name);
    output(
        R"cc(
          bool $0::set_$1($2 key, $3 value) {
            $6return $4_$8_set(msg_, $7, ($5*)value->msg(), arena_);
          }
        )cc",
        class_name, resolved_field_name, CppConstType(key),
        MessagePtrConstType(val, /* is_const */ false), MessageName(message),
        MessageName(val->message_type()), optional_conversion_code,
        converted_key_name, upbc_name);
    output(
        R"cc(
          absl::StatusOr<$3> $0::get_$1($2 key) {
            $5* msg_value;
            $7bool success = $4_$9_get(msg_, $8, &msg_value);
            if (success) {
              return ::protos::internal::CreateMessage<$6>(msg_value);
            }
            return absl::NotFoundError("");
          }
        )cc",
        class_name, resolved_field_name, CppConstType(key),
        MessagePtrConstType(val, /* is_const */ true), MessageName(message),
        MessageName(val->message_type()),
        QualifiedClassName(val->message_type()), optional_conversion_code,
        converted_key_name, upbc_name);
    output(
        R"cc(
          void $0::delete_$1($2 key) { $6$4_$8_delete(msg_, $7); }
        )cc",
        class_name, resolved_field_name, CppConstType(key),
        MessagePtrConstType(val, /* is_const */ false), MessageName(message),
        MessageName(val->message_type()), optional_conversion_code,
        converted_key_name, upbc_name);
  } else if (val->cpp_type() == protobuf::FieldDescriptor::CPPTYPE_STRING) {
    output(
        R"cc(
          bool $0::set_$1($2 key, $3 value) {
            $5return $4_$7_set(msg_, $6,
                               ::protos::UpbStrFromStringView(value, arena_),
                               arena_);
          }
        )cc",
        class_name, resolved_field_name, CppConstType(key), CppConstType(val),
        MessageName(message), optional_conversion_code, converted_key_name,
        upbc_name);
    output(
        R"cc(
          absl::StatusOr<$3> $0::get_$1($2 key) {
            upb_StringView value;
            $5bool success = $4_$7_get(msg_, $6, &value);
            if (success) {
              return absl::string_view(value.data, value.size);
            }
            return absl::NotFoundError("");
          }
        )cc",
        class_name, resolved_field_name, CppConstType(key), CppConstType(val),
        MessageName(message), optional_conversion_code, converted_key_name,
        upbc_name);
    output(
        R"cc(
          void $0::delete_$1($2 key) { $5$4_$7_delete(msg_, $6); }
        )cc",
        class_name, resolved_field_name, CppConstType(key), CppConstType(val),
        MessageName(message), optional_conversion_code, converted_key_name,
        upbc_name);
  } else {
    output(
        R"cc(
          bool $0::set_$1($2 key, $3 value) {
            $5return $4_$7_set(msg_, $6, value, arena_);
          }
        )cc",
        class_name, resolved_field_name, CppConstType(key), CppConstType(val),
        MessageName(message), optional_conversion_code, converted_key_name,
        upbc_name);
    output(
        R"cc(
          absl::StatusOr<$3> $0::get_$1($2 key) {
            $3 value;
            $5bool success = $4_$7_get(msg_, $6, &value);
            if (success) {
              return value;
            }
            return absl::NotFoundError("");
          }
        )cc",
        class_name, resolved_field_name, CppConstType(key), CppConstType(val),
        MessageName(message), optional_conversion_code, converted_key_name,
        upbc_name);
    output(
        R"cc(
          void $0::delete_$1($2 key) { $5$4_$7_delete(msg_, $6); }
        )cc",
        class_name, resolved_field_name, CppConstType(key), CppConstType(val),
        MessageName(message), optional_conversion_code, converted_key_name,
        upbc_name);
  }
}

void WriteUsingAccessorsInHeader(const protobuf::Descriptor* desc,
                                 MessageClassType handle_type, Output& output) {
  bool read_only = handle_type == MessageClassType::kMessageCProxy;

  // Generate const methods.
  OutputIndenter i(output);
  std::string class_name = ClassName(desc);
  auto field_names = CreateFieldNameMap(desc);

  for (const auto* field : FieldNumberOrder(desc)) {
    std::string resolved_field_name = ResolveFieldName(field, field_names);
    // Generate hazzer (if any).
    if (field->has_presence()) {
      output("using $0Access::has_$1;\n", class_name, resolved_field_name);
      output("using $0Access::clear_$1;\n", class_name, resolved_field_name);
    }
    if (field->is_map()) {
      output(
          R"cc(
            using $0Access::$1_size;
            using $0Access::clear_$1;
            using $0Access::delete_$1;
            using $0Access::get_$1;
            using $0Access::set_$1;
          )cc",
          class_name, resolved_field_name);
    } else if (desc->options().map_entry()) {
      // TODO(b/237399867) Implement map entry
    } else if (field->is_repeated()) {
      if (field->cpp_type() == protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
        output(
            R"cc(
              using $0Access::$1;
              using $0Access::$1_size;
              using $0Access::mutable_$1;
            )cc",
            class_name, resolved_field_name);
        if (!read_only) {
          output(
              R"cc(
                using $0Access::add_$1;
                using $0Access::clear_$1;
              )cc",
              class_name, resolved_field_name);
        }
      } else {
        output(
            R"cc(
              using $0Access::$1;
              using $0Access::$1_size;
            )cc",
            class_name, resolved_field_name);
        if (!read_only) {
          output(
              R"cc(
                using $0Access::add_$1;
                using $0Access::clear_$1;
                using $0Access::resize_$1;
                using $0Access::set_$1;
              )cc",
              class_name, resolved_field_name);
        }
      }
    } else {
      if (field->cpp_type() == protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
        output("using $0Access::$1;\n", ClassName(desc), resolved_field_name);
        if (!read_only) {
          output("using $0Access::mutable_$1;\n", class_name,
                 resolved_field_name);
        }
      } else {
        output("using $0Access::$1;\n", class_name, resolved_field_name);
        if (!read_only) {
          output("using $0Access::set_$1;\n", class_name, resolved_field_name);
        }
      }
    }
  }
  output("using $0Access::msg;\n", class_name);
}

std::string ResolveFieldName(const protobuf::FieldDescriptor* field,
                             const NameToFieldDescriptorMap& field_names) {
  // C++ implementation specific reserved names.
  static const auto& kReservedNames =
      *new absl::flat_hash_set<absl::string_view>({
          "msg",
          "msg_",
          "arena",
          "arena_",
      });

  static constexpr absl::string_view kClearAccessor = "clear_";
  static constexpr absl::string_view kSetAccessor = "set_";

  // List of generated accessor prefixes to check against.
  // Example:
  //     optional repeated string phase = 236;
  //     optional bool clear_phase = 237;
  static constexpr absl::string_view kAccessorPrefixes[] = {
      kClearAccessor, "delete_", "add_", "resize_", kSetAccessor,
  };

  absl::string_view field_name = field->name();
  if (kReservedNames.count(field_name) > 0) {
    if (absl::EndsWith(field_name, "_")) {
      return absl::StrCat(field_name, "_");
    } else {
      return absl::StrCat(field_name, "__");
    }
  }
  for (const auto prefix : kAccessorPrefixes) {
    // If field name starts with a prefix such as clear_ and the proto
    // contains a field name with trailing end, depending on type of field
    // (repeated, map, message) we have a conflict to resolve.
    if (absl::StartsWith(field_name, prefix)) {
      auto match = field_names.find(field_name.substr(prefix.size()));
      if (match != field_names.end()) {
        const auto* candidate = match->second;
        if (candidate->is_repeated() || candidate->is_map() ||
            (candidate->cpp_type() ==
                 protobuf::FieldDescriptor::CPPTYPE_STRING &&
             prefix == kClearAccessor) ||
            prefix == kSetAccessor) {
          return absl::StrCat(field_name, "_");
        }
      }
    }
  }
  return upbc::ResolveKeywordConflict(std::string(field_name));
}

}  // namespace protos_generator
