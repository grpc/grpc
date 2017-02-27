/*
 *
 * Copyright 2017, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_CORE_LIB_CONFIG_SCHEMA_H
#define GRPC_CORE_LIB_CONFIG_SCHEMA_H

#include "src/core/lib/config/schema_fwd.h"

/* Config schema definition.
   A schema declares types and expected fields for a configuration. */

typedef struct grpc_config_schema_enum grpc_config_schema_enum;

/* Get a named schema (or create it if it doesn't exist) */
grpc_config_schema *grpc_config_schema_get_or_create(const char *name);

/* Add an integer element to a schema */
grpc_config_key_int *grpc_config_schema_add_int(grpc_config_schema *schema,
                                                const char *name,
                                                int default_value,
                                                int min_value, int max_value);

/* Add a string element to a schema */
grpc_config_key_string *grpc_config_schema_add_string(
    grpc_config_schema *schema, const char *name, const char *default_value);

/* Add an object type to a schema */
grpc_config_key_object *grpc_config_schema_add_object(
    grpc_config_schema *schema, const char *name,
    grpc_config_schema *object_schema);

/* Add an array of objects to a schema */
grpc_config_key_object_array *grpc_config_schema_add_object_array(
    grpc_config_schema *schema, const char *name,
    grpc_config_schema *object_schema);

/* Add an integer array to a schema */
grpc_config_key_int_array *grpc_config_schema_add_int_array(
    grpc_config_schema *schema, const char *name, int min_value, int max_value);

/* Add an array of strings to a schema */
grpc_config_key_string_array *grpc_config_schema_add_string_array(
    grpc_config_schema *schema, const char *name);

#endif /* GRPC_CORE_LIB_CONFIG_SCHEMA_H */
