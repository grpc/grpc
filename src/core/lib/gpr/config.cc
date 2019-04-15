/*
 *
 * Copyright 2019 gRPC authors.
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

#include "src/core/lib/gpr/config.h"
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void gpr_config_error_default_function(const char* error_message);
static gpr_atm g_config_error_func = (gpr_atm)gpr_config_error_default_function;

void gpr_set_config_error_function(gpr_config_error_func func) {
  gpr_atm_no_barrier_store(&g_config_error_func, (gpr_atm)func);
}

static const char* gpr_config_envvar_ensure_canonical_name(char* name) {
  // ensures name is comprised of uppercase letters.
  // this runs everytime but this is not performance-senstivie code
  // so let's keep this simple.
  for (char* c = name; *c != 0; ++c) {
    if (*c >= 'a' && *c <= 'z') {
      *c -= 'a' - 'A';
    }
  }
  return name;
}

// Get the value of the environment variable with the gien name.
// Returned value should be freed by caller.
static char* gpr_config_envvar_get_value(char* name) {
  return gpr_getenv(gpr_config_envvar_ensure_canonical_name(name));
}

// Set the environment variable to have given value.
static void gpr_config_envvar_set_value(char* name, const char* value) {
  gpr_setenv(gpr_config_envvar_ensure_canonical_name(name), value);
}

// Checks whether the environment variable with given name exists.
static bool gpr_config_envvar_check(char* name) {
  char* value = gpr_getenv(gpr_config_envvar_ensure_canonical_name(name));
  if (value) {
    gpr_free(value);
    return true;
  }
  return false;
}

// Removes the variable name from the environment.
static void gpr_config_envvar_reset(char* name) {
  gpr_unsetenv(gpr_config_envvar_ensure_canonical_name(name));
}

static void gpr_config_error_default_function(const char* error_message) {
  gpr_log(GPR_ERROR, "%s", error_message);
  abort();
}

static void gpr_config_log_parsing_error(const char* name, const char* value) {
  char* error_message;
  gpr_asprintf(&error_message,
               "Illegal value '%s' specified for environment variable '%s'",
               value, name);
  ((gpr_config_error_func)gpr_atm_no_barrier_load(&g_config_error_func))(
      error_message);
  gpr_free(error_message);
}

bool _GPR_CONFIG_ENNVAR_GETTER_NAME(bool)(
    const _GPR_CONFIG_ENVVAR_STRUCT_NAME(bool) * decl) {
  char* value = gpr_config_envvar_get_value(decl->name);
  if (value == nullptr) {
    return decl->default_value;
  }
  // empty value means true.
  if (strlen(value) == 0) {
    gpr_free(value);
    return true;
  }
  // parsing given value string.
  bool result = false;
  if (!gpr_parse_bool_value(value, &result)) {
    gpr_config_log_parsing_error(decl->name, value);
  }
  gpr_free(value);
  return result;
}

void _GPR_CONFIG_ENNVAR_SETTER_NAME(bool)(
    const _GPR_CONFIG_ENVVAR_STRUCT_NAME(bool) * decl, const bool value) {
  gpr_config_envvar_set_value(decl->name, value ? "true" : "false");
}

bool _GPR_CONFIG_ENNVAR_CHECKER_NAME(bool)(
    const _GPR_CONFIG_ENVVAR_STRUCT_NAME(bool) * decl) {
  return gpr_config_envvar_check(decl->name);
}

void _GPR_CONFIG_ENNVAR_RESETTER_NAME(bool)(
    const _GPR_CONFIG_ENVVAR_STRUCT_NAME(bool) * decl) {
  return gpr_config_envvar_reset(decl->name);
}

int32_t _GPR_CONFIG_ENNVAR_GETTER_NAME(int32)(
    const _GPR_CONFIG_ENVVAR_STRUCT_NAME(int32) * decl) {
  char* value = gpr_config_envvar_get_value(decl->name);
  if (value == nullptr) {
    return decl->default_value;
  }
  // parsing given value string.
  char* end = value;
  long result = strtol(value, &end, 10);
  if (*end != 0) {
    gpr_config_log_parsing_error(decl->name, value);
  }
  gpr_free(value);
  return static_cast<int32_t>(result);
}

void _GPR_CONFIG_ENNVAR_SETTER_NAME(int32)(
    const _GPR_CONFIG_ENVVAR_STRUCT_NAME(int32) * decl, const int32_t value) {
  char buffer[GPR_LTOA_MIN_BUFSIZE];
  gpr_ltoa(value, buffer);
  gpr_config_envvar_set_value(decl->name, buffer);
}

bool _GPR_CONFIG_ENNVAR_CHECKER_NAME(int32)(
    const _GPR_CONFIG_ENVVAR_STRUCT_NAME(int32) * decl) {
  return gpr_config_envvar_check(decl->name);
}

void _GPR_CONFIG_ENNVAR_RESETTER_NAME(int32)(
    const _GPR_CONFIG_ENVVAR_STRUCT_NAME(int32) * decl) {
  return gpr_config_envvar_reset(decl->name);
}

char* _GPR_CONFIG_ENNVAR_GETTER_NAME(string)(
    const _GPR_CONFIG_ENVVAR_STRUCT_NAME(string) * decl) {
  char* value = gpr_config_envvar_get_value(decl->name);
  if (value == nullptr) {
    GPR_ASSERT(decl->default_value != nullptr);
    return gpr_strdup(decl->default_value);
  }
  return value;
}

void _GPR_CONFIG_ENNVAR_SETTER_NAME(string)(
    const _GPR_CONFIG_ENVVAR_STRUCT_NAME(string) * decl, const char* value) {
  gpr_config_envvar_set_value(decl->name, value);
}

bool _GPR_CONFIG_ENNVAR_CHECKER_NAME(string)(
    const _GPR_CONFIG_ENVVAR_STRUCT_NAME(string) * decl) {
  return gpr_config_envvar_check(decl->name);
}

void _GPR_CONFIG_ENNVAR_RESETTER_NAME(string)(
    const _GPR_CONFIG_ENVVAR_STRUCT_NAME(string) * decl) {
  return gpr_config_envvar_reset(decl->name);
}
