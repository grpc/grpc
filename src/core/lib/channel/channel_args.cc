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

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"

#include <limits.h>
#include <string.h>

#include <map>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/match.h"

namespace {

int PointerCompare(void* a_ptr, const grpc_arg_pointer_vtable* a_vtable,
                   void* b_ptr, const grpc_arg_pointer_vtable* b_vtable) {
  int c = grpc_core::QsortCompare(a_ptr, b_ptr);
  if (c == 0) return 0;
  c = grpc_core::QsortCompare(a_vtable, b_vtable);
  if (c != 0) return c;
  return a_vtable->cmp(a_ptr, b_ptr);
}

}  // namespace

namespace grpc_core {

bool ChannelArgs::Pointer::operator==(const Pointer& rhs) const {
  return PointerCompare(p_, vtable_, rhs.p_, rhs.vtable_) == 0;
}

bool ChannelArgs::Pointer::operator<(const Pointer& rhs) const {
  return PointerCompare(p_, vtable_, rhs.p_, rhs.vtable_) < 0;
}

ChannelArgs::ChannelArgs() = default;

ChannelArgs ChannelArgs::Set(grpc_arg arg) const {
  switch (arg.type) {
    case GRPC_ARG_INTEGER:
      return Set(arg.key, arg.value.integer);
    case GRPC_ARG_STRING:
      if (arg.value.string != nullptr) return Set(arg.key, arg.value.string);
      return Set(arg.key, "");
    case GRPC_ARG_POINTER:
      return Set(arg.key,
                 Pointer(arg.value.pointer.vtable->copy(arg.value.pointer.p),
                         arg.value.pointer.vtable));
  }
  GPR_UNREACHABLE_CODE(return ChannelArgs());
}

ChannelArgs ChannelArgs::FromC(const grpc_channel_args* args) {
  ChannelArgs result;
  if (args != nullptr) {
    for (size_t i = 0; i < args->num_args; i++) {
      result = result.Set(args->args[i]);
    }
  }
  return result;
}

const grpc_channel_args* ChannelArgs::ToC() const {
  std::vector<grpc_arg> c_args;
  args_.ForEach([&c_args](const std::string& key, const Value& value) {
    char* name = const_cast<char*>(key.c_str());
    c_args.push_back(Match(
        value,
        [name](int i) { return grpc_channel_arg_integer_create(name, i); },
        [name](const std::string& s) {
          return grpc_channel_arg_string_create(name,
                                                const_cast<char*>(s.c_str()));
        },
        [name](const Pointer& p) {
          return grpc_channel_arg_pointer_create(name, p.c_pointer(),
                                                 p.c_vtable());
        }));
  });
  return grpc_channel_args_copy_and_add(nullptr, c_args.data(), c_args.size());
}

ChannelArgs ChannelArgs::Set(absl::string_view key, Value value) const {
  return ChannelArgs(args_.Add(std::string(key), std::move(value)));
}

ChannelArgs ChannelArgs::Set(absl::string_view key,
                             absl::string_view value) const {
  return Set(key, std::string(value));
}

ChannelArgs ChannelArgs::Set(absl::string_view key, const char* value) const {
  return Set(key, std::string(value));
}

ChannelArgs ChannelArgs::Set(absl::string_view key, std::string value) const {
  return Set(key, Value(std::move(value)));
}

ChannelArgs ChannelArgs::Remove(absl::string_view key) const {
  return ChannelArgs(args_.Remove(key));
}

absl::optional<int> ChannelArgs::GetInt(absl::string_view name) const {
  auto* v = Get(name);
  if (v == nullptr) return absl::nullopt;
  if (!absl::holds_alternative<int>(*v)) return absl::nullopt;
  return absl::get<int>(*v);
}

absl::optional<Duration> ChannelArgs::GetDurationFromIntMillis(
    absl::string_view name) const {
  auto ms = GetInt(name);
  if (!ms.has_value()) return absl::nullopt;
  if (*ms == INT_MAX) return Duration::Infinity();
  if (*ms == INT_MIN) return Duration::NegativeInfinity();
  return Duration::Milliseconds(*ms);
}

absl::optional<absl::string_view> ChannelArgs::GetString(
    absl::string_view name) const {
  auto* v = Get(name);
  if (v == nullptr) return absl::nullopt;
  if (!absl::holds_alternative<std::string>(*v)) return absl::nullopt;
  return absl::get<std::string>(*v);
}

void* ChannelArgs::GetVoidPointer(absl::string_view name) const {
  auto* v = Get(name);
  if (v == nullptr) return nullptr;
  if (!absl::holds_alternative<Pointer>(*v)) return nullptr;
  return absl::get<Pointer>(*v).c_pointer();
}

}  // namespace grpc_core

static grpc_arg copy_arg(const grpc_arg* src) {
  grpc_arg dst;
  dst.type = src->type;
  dst.key = gpr_strdup(src->key);
  switch (dst.type) {
    case GRPC_ARG_STRING:
      dst.value.string = gpr_strdup(src->value.string);
      break;
    case GRPC_ARG_INTEGER:
      dst.value.integer = src->value.integer;
      break;
    case GRPC_ARG_POINTER:
      dst.value.pointer = src->value.pointer;
      dst.value.pointer.p =
          src->value.pointer.vtable->copy(src->value.pointer.p);
      break;
  }
  return dst;
}

grpc_channel_args* grpc_channel_args_copy_and_add(const grpc_channel_args* src,
                                                  const grpc_arg* to_add,
                                                  size_t num_to_add) {
  return grpc_channel_args_copy_and_add_and_remove(src, nullptr, 0, to_add,
                                                   num_to_add);
}

grpc_channel_args* grpc_channel_args_copy_and_remove(
    const grpc_channel_args* src, const char** to_remove,
    size_t num_to_remove) {
  return grpc_channel_args_copy_and_add_and_remove(src, to_remove,
                                                   num_to_remove, nullptr, 0);
}

static bool should_remove_arg(const grpc_arg* arg, const char** to_remove,
                              size_t num_to_remove) {
  for (size_t i = 0; i < num_to_remove; ++i) {
    if (strcmp(arg->key, to_remove[i]) == 0) return true;
  }
  return false;
}

grpc_channel_args* grpc_channel_args_copy_and_add_and_remove(
    const grpc_channel_args* src, const char** to_remove, size_t num_to_remove,
    const grpc_arg* to_add, size_t num_to_add) {
  // Figure out how many args we'll be copying.
  size_t num_args_to_copy = 0;
  if (src != nullptr) {
    for (size_t i = 0; i < src->num_args; ++i) {
      if (!should_remove_arg(&src->args[i], to_remove, num_to_remove)) {
        ++num_args_to_copy;
      }
    }
  }
  // Create result.
  grpc_channel_args* dst =
      static_cast<grpc_channel_args*>(gpr_malloc(sizeof(grpc_channel_args)));
  dst->num_args = num_args_to_copy + num_to_add;
  if (dst->num_args == 0) {
    dst->args = nullptr;
    return dst;
  }
  dst->args =
      static_cast<grpc_arg*>(gpr_malloc(sizeof(grpc_arg) * dst->num_args));
  // Copy args from src that are not being removed.
  size_t dst_idx = 0;
  if (src != nullptr) {
    for (size_t i = 0; i < src->num_args; ++i) {
      if (!should_remove_arg(&src->args[i], to_remove, num_to_remove)) {
        dst->args[dst_idx++] = copy_arg(&src->args[i]);
      }
    }
  }
  // Add args from to_add.
  for (size_t i = 0; i < num_to_add; ++i) {
    dst->args[dst_idx++] = copy_arg(&to_add[i]);
  }
  GPR_ASSERT(dst_idx == dst->num_args);
  return dst;
}

grpc_channel_args* grpc_channel_args_copy(const grpc_channel_args* src) {
  return grpc_channel_args_copy_and_add(src, nullptr, 0);
}

grpc_channel_args* grpc_channel_args_union(const grpc_channel_args* a,
                                           const grpc_channel_args* b) {
  if (a == nullptr) return grpc_channel_args_copy(b);
  if (b == nullptr) return grpc_channel_args_copy(a);
  const size_t max_out = (a->num_args + b->num_args);
  grpc_arg* uniques =
      static_cast<grpc_arg*>(gpr_malloc(sizeof(*uniques) * max_out));
  for (size_t i = 0; i < a->num_args; ++i) uniques[i] = a->args[i];

  size_t uniques_idx = a->num_args;
  for (size_t i = 0; i < b->num_args; ++i) {
    const char* b_key = b->args[i].key;
    if (grpc_channel_args_find(a, b_key) == nullptr) {  // not found
      uniques[uniques_idx++] = b->args[i];
    }
  }
  grpc_channel_args* result =
      grpc_channel_args_copy_and_add(nullptr, uniques, uniques_idx);
  gpr_free(uniques);
  return result;
}

static int cmp_arg(const grpc_arg* a, const grpc_arg* b) {
  int c = grpc_core::QsortCompare(a->type, b->type);
  if (c != 0) return c;
  c = strcmp(a->key, b->key);
  if (c != 0) return c;
  switch (a->type) {
    case GRPC_ARG_STRING:
      return strcmp(a->value.string, b->value.string);
    case GRPC_ARG_INTEGER:
      return grpc_core::QsortCompare(a->value.integer, b->value.integer);
    case GRPC_ARG_POINTER:
      return PointerCompare(a->value.pointer.p, a->value.pointer.vtable,
                            b->value.pointer.p, b->value.pointer.vtable);
  }
  GPR_UNREACHABLE_CODE(return 0);
}

/* stabilizing comparison function: since channel_args ordering matters for
 * keys with the same name, we need to preserve that ordering */
static int cmp_key_stable(const void* ap, const void* bp) {
  const grpc_arg* const* a = static_cast<const grpc_arg* const*>(ap);
  const grpc_arg* const* b = static_cast<const grpc_arg* const*>(bp);
  int c = strcmp((*a)->key, (*b)->key);
  if (c == 0) c = grpc_core::QsortCompare(*a, *b);
  return c;
}

grpc_channel_args* grpc_channel_args_normalize(const grpc_channel_args* src) {
  grpc_arg** args =
      static_cast<grpc_arg**>(gpr_malloc(sizeof(grpc_arg*) * src->num_args));
  for (size_t i = 0; i < src->num_args; i++) {
    args[i] = &src->args[i];
  }
  if (src->num_args > 1) {
    qsort(args, src->num_args, sizeof(grpc_arg*), cmp_key_stable);
  }

  grpc_channel_args* b =
      static_cast<grpc_channel_args*>(gpr_malloc(sizeof(grpc_channel_args)));
  b->num_args = src->num_args;
  b->args = static_cast<grpc_arg*>(gpr_malloc(sizeof(grpc_arg) * b->num_args));
  for (size_t i = 0; i < src->num_args; i++) {
    b->args[i] = copy_arg(args[i]);
  }

  gpr_free(args);
  return b;
}

void grpc_channel_args_destroy(grpc_channel_args* a) {
  size_t i;
  if (!a) return;
  for (i = 0; i < a->num_args; i++) {
    switch (a->args[i].type) {
      case GRPC_ARG_STRING:
        gpr_free(a->args[i].value.string);
        break;
      case GRPC_ARG_INTEGER:
        break;
      case GRPC_ARG_POINTER:
        a->args[i].value.pointer.vtable->destroy(a->args[i].value.pointer.p);
        break;
    }
    gpr_free(a->args[i].key);
  }
  gpr_free(a->args);
  gpr_free(a);
}

int grpc_channel_args_compare(const grpc_channel_args* a,
                              const grpc_channel_args* b) {
  if (a == nullptr && b == nullptr) return 0;
  if (a == nullptr || b == nullptr) return a == nullptr ? -1 : 1;
  int c = grpc_core::QsortCompare(a->num_args, b->num_args);
  if (c != 0) return c;
  for (size_t i = 0; i < a->num_args; i++) {
    c = cmp_arg(&a->args[i], &b->args[i]);
    if (c != 0) return c;
  }
  return 0;
}

const grpc_arg* grpc_channel_args_find(const grpc_channel_args* args,
                                       const char* name) {
  if (args != nullptr) {
    for (size_t i = 0; i < args->num_args; ++i) {
      if (strcmp(args->args[i].key, name) == 0) {
        return &args->args[i];
      }
    }
  }
  return nullptr;
}

int grpc_channel_arg_get_integer(const grpc_arg* arg,
                                 const grpc_integer_options options) {
  if (arg == nullptr) return options.default_value;
  if (arg->type != GRPC_ARG_INTEGER) {
    gpr_log(GPR_ERROR, "%s ignored: it must be an integer", arg->key);
    return options.default_value;
  }
  if (arg->value.integer < options.min_value) {
    gpr_log(GPR_ERROR, "%s ignored: it must be >= %d", arg->key,
            options.min_value);
    return options.default_value;
  }
  if (arg->value.integer > options.max_value) {
    gpr_log(GPR_ERROR, "%s ignored: it must be <= %d", arg->key,
            options.max_value);
    return options.default_value;
  }
  return arg->value.integer;
}

int grpc_channel_args_find_integer(const grpc_channel_args* args,
                                   const char* name,
                                   const grpc_integer_options options) {
  const grpc_arg* arg = grpc_channel_args_find(args, name);
  return grpc_channel_arg_get_integer(arg, options);
}

char* grpc_channel_arg_get_string(const grpc_arg* arg) {
  if (arg == nullptr) return nullptr;
  if (arg->type != GRPC_ARG_STRING) {
    gpr_log(GPR_ERROR, "%s ignored: it must be an string", arg->key);
    return nullptr;
  }
  return arg->value.string;
}

char* grpc_channel_args_find_string(const grpc_channel_args* args,
                                    const char* name) {
  const grpc_arg* arg = grpc_channel_args_find(args, name);
  return grpc_channel_arg_get_string(arg);
}

bool grpc_channel_arg_get_bool(const grpc_arg* arg, bool default_value) {
  if (arg == nullptr) return default_value;
  if (arg->type != GRPC_ARG_INTEGER) {
    gpr_log(GPR_ERROR, "%s ignored: it must be an integer", arg->key);
    return default_value;
  }
  switch (arg->value.integer) {
    case 0:
      return false;
    case 1:
      return true;
    default:
      gpr_log(GPR_ERROR, "%s treated as bool but set to %d (assuming true)",
              arg->key, arg->value.integer);
      return true;
  }
}

bool grpc_channel_args_find_bool(const grpc_channel_args* args,
                                 const char* name, bool default_value) {
  const grpc_arg* arg = grpc_channel_args_find(args, name);
  return grpc_channel_arg_get_bool(arg, default_value);
}

bool grpc_channel_args_want_minimal_stack(const grpc_channel_args* args) {
  return grpc_channel_arg_get_bool(
      grpc_channel_args_find(args, GRPC_ARG_MINIMAL_STACK), false);
}

grpc_arg grpc_channel_arg_string_create(char* name, char* value) {
  grpc_arg arg;
  arg.type = GRPC_ARG_STRING;
  arg.key = name;
  arg.value.string = value;
  return arg;
}

grpc_arg grpc_channel_arg_integer_create(char* name, int value) {
  grpc_arg arg;
  arg.type = GRPC_ARG_INTEGER;
  arg.key = name;
  arg.value.integer = value;
  return arg;
}

grpc_arg grpc_channel_arg_pointer_create(
    char* name, void* value, const grpc_arg_pointer_vtable* vtable) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.key = name;
  arg.value.pointer.p = value;
  arg.value.pointer.vtable = vtable;
  return arg;
}

std::string grpc_channel_args_string(const grpc_channel_args* args) {
  if (args == nullptr) return "";
  std::vector<std::string> arg_strings;
  for (size_t i = 0; i < args->num_args; ++i) {
    const grpc_arg& arg = args->args[i];
    std::string arg_string;
    switch (arg.type) {
      case GRPC_ARG_INTEGER:
        arg_string = absl::StrFormat("%s=%d", arg.key, arg.value.integer);
        break;
      case GRPC_ARG_STRING:
        arg_string = absl::StrFormat("%s=%s", arg.key, arg.value.string);
        break;
      case GRPC_ARG_POINTER:
        arg_string = absl::StrFormat("%s=%p", arg.key, arg.value.pointer.p);
        break;
      default:
        arg_string = "arg with unknown type";
    }
    arg_strings.push_back(arg_string);
  }
  return absl::StrJoin(arg_strings, ", ");
}

namespace grpc_core {
ChannelArgs ChannelArgsBuiltinPrecondition(const grpc_channel_args* src) {
  if (src == nullptr) return ChannelArgs();
  ChannelArgs output;
  std::map<absl::string_view, std::vector<absl::string_view>>
      concatenated_values;
  for (size_t i = 0; i < src->num_args; i++) {
    absl::string_view key = src->args[i].key;
    // User-agent strings were traditionally multi-valued and concatenated.
    // We preserve this behavior for backwards compatibility.
    if (key == GRPC_ARG_PRIMARY_USER_AGENT_STRING ||
        key == GRPC_ARG_SECONDARY_USER_AGENT_STRING) {
      if (src->args[i].type != GRPC_ARG_STRING) {
        gpr_log(GPR_ERROR, "Channel argument '%s' should be a string",
                std::string(key).c_str());
      } else {
        concatenated_values[key].push_back(src->args[i].value.string);
      }
      continue;
    } else if (absl::StartsWith(key, "grpc.internal.")) {
      continue;
    }
    if (!output.Contains(key)) {
      output = output.Set(src->args[i]);
    } else {
      // Traditional grpc_channel_args_find behavior was to pick the first
      // value.
      // For compatibility with existing users, we will do the same here.
    }
  }
  // Concatenate the concatenated values.
  for (const auto& concatenated_value : concatenated_values) {
    output = output.Set(concatenated_value.first,
                        absl::StrJoin(concatenated_value.second, " "));
  }
  return output;
}
}  // namespace grpc_core

namespace {
grpc_channel_args_client_channel_creation_mutator g_mutator = nullptr;
}  // namespace

void grpc_channel_args_set_client_channel_creation_mutator(
    grpc_channel_args_client_channel_creation_mutator cb) {
  GPR_DEBUG_ASSERT(g_mutator == nullptr);
  g_mutator = cb;
}
grpc_channel_args_client_channel_creation_mutator
grpc_channel_args_get_client_channel_creation_mutator() {
  return g_mutator;
}
