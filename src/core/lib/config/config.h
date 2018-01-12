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

#ifndef GRPC_CORE_LIB_CONFIG_CONFIG_H
#define GRPC_CORE_LIB_CONFIG_CONFIG_H

/* Validated configuration retrieval API */

#include "src/core/lib/config/config_fwd.h"
#include "src/core/lib/config/schema_fwd.h"

#include <stddef.h>

/* Retrieve an integer */
int grpc_config_get_int(grpc_config *config, grpc_config_key_int *key);

/* Retrieve a string */
const char *grpc_config_get_string(grpc_config *config,
                                   grpc_config_key_string *key);

/* Retrieve an object */
grpc_config *grpc_config_get_object(grpc_config *config,
                                    grpc_config_key_object *key);

/* Retrieve an integer array size */
size_t grpc_config_get_int_array_size(grpc_config *config,
                                      grpc_config_key_int_array *key);

/* Retrieve a string array size */
size_t grpc_config_get_string_array_size(grpc_config *config,
                                         grpc_config_key_string_array *key);

/* Retrieve an object array size */
size_t grpc_config_get_object_array_size(grpc_config *config,
                                         grpc_config_key_object_array *key);

/* Retrieve an integer array element */
int grpc_config_get_int_array_element(grpc_config *config,
                                      grpc_config_key_int_array *key,
                                      size_t idx);

/* Retrieve a string array element */
const char *grpc_config_get_string_array_element(
    grpc_config *config, grpc_config_key_string_array *key, size_t idx);

/* Retrieve an object array element */
grpc_config *grpc_config_get_object_array_element(
    grpc_config *config, grpc_config_key_object_array *key, size_t idx);

/* Iterate an integer array */
void grpc_config_int_array_foreach(grpc_config *config,
                                   grpc_config_key_int_array *key,
                                   void (*callback)(void *user_data, int value),
                                   void *user_data);

/* Iterate a string array */
void grpc_config_string_array_foreach(
    grpc_config *config, grpc_config_key_string_array *key,
    void (*callback)(void *user_data, const char *value), void *user_data);

/* Iterate an object array */
void grpc_config_object_array_foreach(
    grpc_config *config, grpc_config_key_object_array *key,
    void (*callback)(void *user_data, grpc_config *value), void *user_data);

#endif /* GRPC_CORE_LIB_CONFIG_CONFIG_H */
