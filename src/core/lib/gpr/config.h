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

#ifndef GRPC_CORE_LIB_GPR_CONFIG_H
#define GRPC_CORE_LIB_GPR_CONFIG_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

// --------------------------------------------------------------------
// How to use configuration variables:
//
// Defining config variables of a specified type:
//   GPR_CONFIG_DEFINE_*TYPE*(name, default_value, help);
//
// Supported TYPEs: BOOL, INT32, STRING
//
// It's recommended to use lowercase letters for 'name' like
// regular variables. The builtin configuration system uses
// environment variable and the name is converted to uppercase
// when looking up the value. For example,
// GPR_CONFIG_DEFINE(grpc_latency) looks up the value with the
// name, "GRPC_LATENCY".
//
// The variable initially has the specified 'default_value'
// which must be an expression convertible to 'Type'.
// 'default_value' may be evaluated 0 or more times,
// and at an unspecified time; keep it
// simple and usually free of side-effects.
//
// GPR_CONFIG_DEFINE_*TYPE* should not be called in a C++ header.
// It should be called at the top-level (outside any namespaces)
// in a .cc file.
//
// Getting the variables:
//   GPR_CONFIG_GET(name)
//
// If error happens during getting variables, it may end up with
// aborting with the error message. For STRING variables, returned
// string should be freed by a caller.
//
// Setting the variables with new value:
//   GPR_CONFIG_SET(name, new_value)
//
// Declaring config variables for other modules to access:
//   GPR_CONFIG_DECLARE_*TYPE*(name)

// --------------------------------------------------------------------
// How to customize the configuration system:
//
// How to read and write configuration value can be customized.
// Builtin system uses environment variables but it can be extended to
// support command-line flag, file, etc.
//
// To customize it, following macros should be defined along with
// GPR_CONFIG_CUSTOM.
//
//   GPR_CONFIG_DEFINE_BOOL
//   GPR_CONFIG_DEFINE_INT32
//   GPR_CONFIG_DEFINE_STRING
//
// These macros should define functions for getting and setting variable.
// For example, GPR_CONFIG_DEFINE_BOOL(test, ...) would define two functions.
//
//   bool gpr_config_get_test();
//   void gpr_config_set_test(const bool value);

#define _GPR_CONFIG_GETTER_NAME(name) gpr_config_get_##name
#define _GPR_CONFIG_SETTER_NAME(name) gpr_config_set_##name

#define _GPR_CONFIG_DECLARE_BODY(name, value_type)   \
  extern value_type _GPR_CONFIG_GETTER_NAME(name)(); \
  extern void _GPR_CONFIG_SETTER_NAME(name)(const value_type value)

#define GPR_CONFIG_GET(name) _GPR_CONFIG_GETTER_NAME(name)()
#define GPR_CONFIG_SET(name, value) _GPR_CONFIG_SETTER_NAME(name)(value)

#define GPR_CONFIG_DECLARE_BOOL(name) _GPR_CONFIG_DECLARE_BODY(name, bool)
#define GPR_CONFIG_DECLARE_INT32(name) _GPR_CONFIG_DECLARE_BODY(name, int32_t)
#define GPR_CONFIG_DECLARE_STRING(name) _GPR_CONFIG_DECLARE_BODY(name, char*)

// Macro defining the name of type struct
#define _GPR_CONFIG_ENVVAR_STRUCT_NAME(type) gpr_config_envvar_##type

// Macro defining the name of config instance
#define _GPR_CONFIG_ENVVAR_INSTANCE_NAME(name) \
  _gpr_config_envvar_instance_##name

#define _GPR_CONFIG_ENNVAR_GETTER_NAME(type) gpr_config_envvar_get_##type
#define _GPR_CONFIG_ENNVAR_SETTER_NAME(type) gpr_config_envvar_set_##type
#define _GPR_CONFIG_ENNVAR_CHECKER_NAME(type) gpr_config_envvar_check_##type
#define _GPR_CONFIG_ENNVAR_RESETTER_NAME(type) gpr_config_envvar_reset_##type

#define _GPR_CONFIG_ENNVAR_DEFINE_TYPE(type, value_type)  \
  typedef struct {                                        \
    char* name;                                           \
    const value_type default_value;                       \
  } _GPR_CONFIG_ENVVAR_STRUCT_NAME(type);                 \
  extern value_type _GPR_CONFIG_ENNVAR_GETTER_NAME(type)( \
      const _GPR_CONFIG_ENVVAR_STRUCT_NAME(type) * decl); \
  extern void _GPR_CONFIG_ENNVAR_SETTER_NAME(type)(       \
      const _GPR_CONFIG_ENVVAR_STRUCT_NAME(type) * decl,  \
      const value_type value);                            \
  extern bool _GPR_CONFIG_ENNVAR_CHECKER_NAME(type)(      \
      const _GPR_CONFIG_ENVVAR_STRUCT_NAME(type) * decl); \
  extern void _GPR_CONFIG_ENNVAR_RESETTER_NAME(type)(     \
      const _GPR_CONFIG_ENVVAR_STRUCT_NAME(type) * decl);

#define _GPR_CONFIG_ENNVAR_DEFINE_VARIABLE(type, name, default_value, help) \
  static char _gpr_config_envvar_name_##name##_str[] = #name;               \
  _GPR_CONFIG_ENVVAR_STRUCT_NAME(type)                                      \
  _GPR_CONFIG_ENVVAR_INSTANCE_NAME(name) = {                                \
      _gpr_config_envvar_name_##name##_str, default_value}

#define _GPR_CONFIG_ENNVAR_DEFINE_VARIABLE_FUNCTIONS(type, value_type, name) \
  value_type _GPR_CONFIG_GETTER_NAME(name)() {                               \
    return _GPR_CONFIG_ENNVAR_GETTER_NAME(type)(                             \
        &_GPR_CONFIG_ENVVAR_INSTANCE_NAME(name));                            \
  }                                                                          \
  void _GPR_CONFIG_SETTER_NAME(name)(const value_type value) {               \
    _GPR_CONFIG_ENNVAR_SETTER_NAME(type)                                     \
    (&_GPR_CONFIG_ENVVAR_INSTANCE_NAME(name), value);                        \
  }

_GPR_CONFIG_ENNVAR_DEFINE_TYPE(bool, bool);
_GPR_CONFIG_ENNVAR_DEFINE_TYPE(int32, int32_t);
_GPR_CONFIG_ENNVAR_DEFINE_TYPE(string, char*);

// Default environment variables based configuration layer.
#ifndef GPR_CONFIG_CUSTOM

#define GPR_CONFIG_DEFINE_BOOL(name, default_value, help)              \
  _GPR_CONFIG_ENNVAR_DEFINE_VARIABLE(bool, name, default_value, help); \
  _GPR_CONFIG_ENNVAR_DEFINE_VARIABLE_FUNCTIONS(bool, bool, name)

#define GPR_CONFIG_DEFINE_INT32(name, default_value, help)              \
  _GPR_CONFIG_ENNVAR_DEFINE_VARIABLE(int32, name, default_value, help); \
  _GPR_CONFIG_ENNVAR_DEFINE_VARIABLE_FUNCTIONS(int32, int32_t, name)

#define GPR_CONFIG_DEFINE_STRING(name, default_value, help)              \
  _GPR_CONFIG_ENNVAR_DEFINE_VARIABLE(string, name, default_value, help); \
  _GPR_CONFIG_ENNVAR_DEFINE_VARIABLE_FUNCTIONS(string, char*, name)

#endif  // GPR_CONFIG_CUSTOM

typedef void (*gpr_config_error_func)(const char* error_message);

/*
 * Set global config_error_function which is called when config system
 * encounters errors such as parsing error. What the default function does
 * is logging error message and aborting.
 */
void gpr_set_config_error_function(gpr_config_error_func func);

#endif /* GRPC_CORE_LIB_GPR_CONFIG_H */
