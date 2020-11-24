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

#include <limits.h>
#include <string.h>

#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/grpc.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/impl/codegen/log.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"

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
  int c = GPR_ICMP(a->type, b->type);
  if (c != 0) return c;
  c = strcmp(a->key, b->key);
  if (c != 0) return c;
  switch (a->type) {
    case GRPC_ARG_STRING:
      return strcmp(a->value.string, b->value.string);
    case GRPC_ARG_INTEGER:
      return GPR_ICMP(a->value.integer, b->value.integer);
    case GRPC_ARG_POINTER:
      c = GPR_ICMP(a->value.pointer.p, b->value.pointer.p);
      if (c != 0) {
        c = GPR_ICMP(a->value.pointer.vtable, b->value.pointer.vtable);
        if (c == 0) {
          c = a->value.pointer.vtable->cmp(a->value.pointer.p,
                                           b->value.pointer.p);
        }
      }
      return c;
  }
  GPR_UNREACHABLE_CODE(return 0);
}

/* stabilizing comparison function: since channel_args ordering matters for
 * keys with the same name, we need to preserve that ordering */
static int cmp_key_stable(const void* ap, const void* bp) {
  const grpc_arg* const* a = static_cast<const grpc_arg* const*>(ap);
  const grpc_arg* const* b = static_cast<const grpc_arg* const*>(bp);
  int c = strcmp((*a)->key, (*b)->key);
  if (c == 0) c = GPR_ICMP(*a, *b);
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
  int c = GPR_ICMP(a->num_args, b->num_args);
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
