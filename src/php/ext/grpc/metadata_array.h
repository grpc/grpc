/*
 *
 * Copyright 2021 gRPC authors.
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

#ifndef NET_GRPC_PHP_GRPC_METADATA_ARRAY_H_
#define NET_GRPC_PHP_GRPC_METADATA_ARRAY_H_

#include "php_grpc.h"

/* Creates and returns a PHP array object with the data in a
 * grpc_metadata_array. Returns NULL on failure */
zval* grpc_parse_metadata_array(grpc_metadata_array* metadata_array TSRMLS_DC);

/* Populates a grpc_metadata_array with the data in a PHP array object.
   Returns true on success and false on failure */
bool create_metadata_array(zval* array, grpc_metadata_array* metadata);

void grpc_php_metadata_array_destroy_including_entries(
    grpc_metadata_array* array);

#endif /* NET_GRPC_PHP_GRPC_METADATA_ARRAY_H_ */
