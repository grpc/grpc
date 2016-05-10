/*
 *
 * Copyright 2015, Google Inc.
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

#ifndef GRPC_CORE_LIB_SECURITY_UTIL_JSON_UTIL_H
#define GRPC_CORE_LIB_SECURITY_UTIL_JSON_UTIL_H

#include <stdbool.h>

#include "src/core/lib/json/json.h"

// Constants.
#define GRPC_AUTH_JSON_TYPE_INVALID "invalid"
#define GRPC_AUTH_JSON_TYPE_SERVICE_ACCOUNT "service_account"
#define GRPC_AUTH_JSON_TYPE_AUTHORIZED_USER "authorized_user"

// Gets a child property from a json node.
const char *grpc_json_get_string_property(const grpc_json *json,
                                          const char *prop_name);

// Copies the value of the json child property specified by prop_name.
// Returns false if the property was not found.
bool grpc_copy_json_string_property(const grpc_json *json,
                                    const char *prop_name, char **copied_value);

#endif /* GRPC_CORE_LIB_SECURITY_UTIL_JSON_UTIL_H */
