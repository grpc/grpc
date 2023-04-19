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

#include "protos_generator/gen_messages.h"

#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor.h"
#include "protos_generator/gen_accessors.h"
#include "protos_generator/gen_extensions.h"
#include "protos_generator/gen_utils.h"
#include "protos_generator/output.h"
#include "upbc/common.h"
#include "upbc/file_layout.h"

namespace protos_generator {

namespace protobuf = ::google::protobuf;

void WriteModelAccessDeclaration(const protobuf::Descriptor* descriptor,
                                 Output& output);
void WriteModelPublicDeclaration(
    const protobuf::Descriptor* descriptor,
    const std::vector<const protobuf::FieldDescriptor*>& file_exts,
    Output& output);
void WriteExtensionIdentifiersInClassHeader(
    const protobuf::Descriptor* message,
    const std::vector<const protobuf::FieldDescriptor*>& file_exts,
    Output& output);
void WriteModelProxyDeclaration(const protobuf::Descriptor* descriptor,
                                Output& output);
void WriteModelCProxyDeclaration(const protobuf::Descriptor* descriptor,
                                 Output& output);
void WriteInternalForwardDeclarationsInHeader(
    const protobuf::Descriptor* message, Output& output);
void WriteDefaultInstanceHeader(const protobuf::Descriptor* message,
                                Output& output);
void WriteExtensionIdentifiersImplementation(
    const protobuf::Descriptor* message,
    const std::vector<const protobuf::FieldDescriptor*>& file_exts,
    Output& output);

// Writes message class declarations into .upb.proto.h.
//
// For each proto Foo, FooAccess and FooProxy/FooCProxy are generated
// that are exposed to users as Foo , Ptr<Foo> and Ptr<const Foo>.
void WriteMessageClassDeclarations(
    const protobuf::Descriptor* descriptor,
    const std::vector<const protobuf::FieldDescriptor*>& file_exts,
    Output& output) {
  if (IsMapEntryMessage(descriptor)) {
    // Skip map entry generation. Low level accessors for maps are
    // generated that don't require a separate map type.
    return;
  }

  // Forward declaration of Proto Class for GCC handling of free friend method.
  output("class $0;", ClassName(descriptor));
  output("namespace internal {\n");
  WriteModelAccessDeclaration(descriptor, output);
  output("\n");
  WriteInternalForwardDeclarationsInHeader(descriptor, output);
  output("\n");
  output("}  // namespace internal\n");
  WriteModelPublicDeclaration(descriptor, file_exts, output);
  output("namespace internal {\n");
  WriteModelProxyDeclaration(descriptor, output);
  WriteModelCProxyDeclaration(descriptor, output);
  output("}  // namespace internal\n");
}

void WriteModelAccessDeclaration(const protobuf::Descriptor* descriptor,
                                 Output& output) {
  output(
      R"cc(

        class $0Access {
         public:
          $0Access() {}
          $0Access($1* msg, upb_Arena* arena) : msg_(msg), arena_(arena) {}  // NOLINT
          $0Access(const $1* msg, upb_Arena* arena)
              : msg_(const_cast<$1*>(msg)), arena_(arena) {}  // NOLINT
          void* GetInternalArena() const { return arena_; }
      )cc",
      ClassName(descriptor), MessageName(descriptor));
  WriteFieldAccessorsInHeader(descriptor, output);
  output.Indent();
  output(
      R"cc(
        private:
        void* msg() const { return msg_; }

        friend class $2;
        friend class $0Proxy;
        friend class $0CProxy;
        friend void* ::protos::internal::GetInternalMsg<$2>(const $2& message);
        friend void* ::protos::internal::GetInternalMsg<$2>(
            const ::protos::Ptr<$2>& message);
        $1* msg_;
        upb_Arena* arena_;
      )cc",
      ClassName(descriptor), MessageName(descriptor),
      QualifiedClassName(descriptor));
  output.Outdent();
  output("};\n");
}

void WriteModelPublicDeclaration(
    const protobuf::Descriptor* descriptor,
    const std::vector<const protobuf::FieldDescriptor*>& file_exts,
    Output& output) {
  output(
      R"cc(
        class $0 final : private internal::$0Access {
         public:
          using Access = internal::$0Access;
          using Proxy = internal::$0Proxy;
          using CProxy = internal::$0CProxy;
          $0();
          $0(const $0& m) = delete;
          $0& operator=(const $0& m) = delete;
          $0($0&& m)
              : Access(m.msg_, m.arena_),
                owned_arena_(std::move(m.owned_arena_)) {}
          $0& operator=($0&& m) {
            msg_ = m.msg_;
            arena_ = m.arena_;
            m.msg_ = nullptr;
            m.arena_ = nullptr;
            owned_arena_ = std::move(m.owned_arena_);
            return *this;
          }
      )cc",
      ClassName(descriptor));

  WriteUsingAccessorsInHeader(descriptor, MessageClassType::kMessage, output);
  WriteDefaultInstanceHeader(descriptor, output);
  WriteExtensionIdentifiersInClassHeader(descriptor, file_exts, output);
  output.Indent();
  output.Indent();
  if (descriptor->extension_range_count()) {
    // for typetrait checking
    output("using ExtendableType = $0;\n", ClassName(descriptor));
  }
  // Note: free function friends that are templates such as ::protos::Parse
  // require explicit <$2> type parameter in declaration to be able to compile
  // with gcc otherwise the compiler will fail with
  // "has not been declared within namespace" error. Even though there is a
  // namespace qualifier, cross namespace matching fails.
  output(
      R"cc(
        static const upb_MiniTable* minitable();
        using $0Access::GetInternalArena;

        private:
        $0(upb_Message* msg, upb_Arena* arena) : $0Access() {
          msg_ = ($1*)msg;
          arena_ = owned_arena_.ptr();
          upb_Arena_Fuse(arena_, arena);
        }
        ::protos::Arena owned_arena_;
        friend Proxy;
        friend CProxy;
        friend absl::StatusOr<$2>(::protos::Parse<$2>(absl::string_view bytes,
                                                      int options));
        friend absl::StatusOr<$2>(::protos::Parse<$2>(
            absl::string_view bytes,
            const ::protos::ExtensionRegistry& extension_registry,
            int options));
        friend upb_Arena* ::protos::internal::GetArena<$0>(const $0& message);
        friend upb_Arena* ::protos::internal::GetArena<$0>(
            const ::protos::Ptr<$0>& message);
        friend $0(::protos::internal::MoveMessage<$0>(upb_Message* msg,
                                                      upb_Arena* arena));
      )cc",
      ClassName(descriptor), MessageName(descriptor),
      QualifiedClassName(descriptor));
  output.Outdent();
  output("};\n\n");
}

void WriteModelProxyDeclaration(const protobuf::Descriptor* descriptor,
                                Output& output) {
  // Foo::Proxy.
  output(
      R"cc(
        class $0Proxy final : private internal::$0Access {
         public:
          $0Proxy() = delete;
          $0Proxy(const $0Proxy& m) : internal::$0Access() {
            msg_ = m.msg_;
            arena_ = m.arena_;
          }
          $0Proxy operator=(const $0Proxy& m) {
            msg_ = m.msg_;
            arena_ = m.arena_;
            return *this;
          }
          using $0Access::GetInternalArena;
      )cc",
      ClassName(descriptor));

  WriteUsingAccessorsInHeader(descriptor, MessageClassType::kMessageProxy,
                              output);
  output("\n");
  output.Indent(1);
  output(
      R"cc(
        private:
        $0Proxy(void* msg, upb_Arena* arena) : internal::$0Access(($1*)msg, arena) {}
        friend $0::Proxy(::protos::CreateMessage<$0>(::protos::Arena& arena));
        friend $0::Proxy(::protos::internal::CreateMessageProxy<$0>(
            upb_Message*, upb_Arena*));
        friend class $0CProxy;
        friend class $0Access;
        friend class ::protos::Ptr<$0>;
        friend class ::protos::Ptr<const $0>;
        friend upb_Arena* ::protos::internal::GetArena<$2>(const $2& message);
        friend upb_Arena* ::protos::internal::GetArena<$2>(
            const ::protos::Ptr<$2>& message);
        static void Rebind($0Proxy& lhs, const $0Proxy& rhs) {
          lhs.msg_ = rhs.msg_;
          lhs.arena_ = rhs.arena_;
        }
      )cc",
      ClassName(descriptor), MessageName(descriptor),
      QualifiedClassName(descriptor));
  output.Outdent(1);
  output("};\n\n");
}

void WriteModelCProxyDeclaration(const protobuf::Descriptor* descriptor,
                                 Output& output) {
  // Foo::CProxy.
  output(
      R"cc(
        class $0CProxy final : private internal::$0Access {
         public:
          $0CProxy() = delete;
          $0CProxy(const $0* m) : internal::$0Access(m->msg_, nullptr) {}
          using $0Access::GetInternalArena;
      )cc",
      ClassName(descriptor), MessageName(descriptor));

  WriteUsingAccessorsInHeader(descriptor, MessageClassType::kMessageProxy,
                              output);

  output.Indent(1);
  output(
      R"cc(
        private:
        $0CProxy(void* msg) : internal::$0Access(($1*)msg, nullptr){};
        friend $0::CProxy(::protos::internal::CreateMessage<$0>(upb_Message* msg));
        friend class ::protos::Ptr<$0>;
        friend class ::protos::Ptr<const $0>;
        static void Rebind($0CProxy& lhs, const $0CProxy& rhs) {
          lhs.msg_ = rhs.msg_;
          lhs.arena_ = rhs.arena_;
        }
      )cc",
      ClassName(descriptor), MessageName(descriptor));
  output.Outdent(1);
  output("};\n\n");
}

void WriteDefaultInstanceHeader(const protobuf::Descriptor* message,
                                Output& output) {
  output("  static ::protos::Ptr<const $0> default_instance();\n",
         ClassName(message));
}

void WriteMessageImplementation(
    const protobuf::Descriptor* descriptor,
    const std::vector<const protobuf::FieldDescriptor*>& file_exts,
    Output& output) {
  bool message_is_map_entry = descriptor->options().map_entry();
  if (!message_is_map_entry) {
    // Constructor.
    output(
        R"cc(
          $0::$0() : $0Access() {
            arena_ = owned_arena_.ptr();
            msg_ = $1_new(arena_);
          }
        )cc",
        ClassName(descriptor), MessageName(descriptor));
    // Minitable
    OutputIndenter i(output);
    output(
        R"cc(
          const upb_MiniTable* $0::minitable() { return &$1; }
        )cc",
        ClassName(descriptor), ::upbc::MessageInit(descriptor->full_name()));
  }

  WriteAccessorsInSource(descriptor, output);

  if (!message_is_map_entry) {
    output(
        R"cc(
          struct $0DefaultTypeInternal {
            $1* msg;
          };
          $0DefaultTypeInternal _$0_default_instance_ =
              $0DefaultTypeInternal{$1_new(upb_Arena_New())};
        )cc",
        ClassName(descriptor), MessageName(descriptor));

    output(
        R"cc(
          ::protos::Ptr<const $0> $0::default_instance() {
            return ::protos::internal::CreateMessage<$0>(
                (upb_Message *)_$0_default_instance_.msg);
          }
        )cc",
        ClassName(descriptor));
  }
}

void WriteInternalForwardDeclarationsInHeader(
    const protobuf::Descriptor* message, Output& output) {
  // Write declaration for internal re-usable default_instance without
  // leaking implementation.
  output(
      R"cc(
        struct $0DefaultTypeInternal;
        extern $0DefaultTypeInternal _$0_default_instance_;
      )cc",
      ClassName(message));
}

void WriteExtensionIdentifiersInClassHeader(
    const protobuf::Descriptor* message,
    const std::vector<const protobuf::FieldDescriptor*>& file_exts,
    Output& output) {
  for (auto* ext : file_exts) {
    if (ext->extension_scope() &&
        ext->extension_scope()->full_name() == message->full_name()) {
      WriteExtensionIdentifierHeader(ext, output);
    }
  }
}

void WriteExtensionIdentifiersImplementation(
    const protobuf::Descriptor* message,
    const std::vector<const protobuf::FieldDescriptor*>& file_exts,
    Output& output) {
  for (auto* ext : file_exts) {
    if (ext->extension_scope() &&
        ext->extension_scope()->full_name() == message->full_name()) {
      WriteExtensionIdentifier(ext, output);
    }
  }
}

}  // namespace protos_generator
