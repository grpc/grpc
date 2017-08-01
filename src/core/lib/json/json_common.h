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

#ifndef GRPC_CORE_LIB_JSON_JSON_COMMON_H
#define GRPC_CORE_LIB_JSON_JSON_COMMON_H

/* The various json types. */
typedef enum {
  GRPC_JSON_OBJECT,
  GRPC_JSON_ARRAY,
  GRPC_JSON_STRING,
  GRPC_JSON_NUMBER,
  GRPC_JSON_TRUE,
  GRPC_JSON_FALSE,
  GRPC_JSON_NULL,
  GRPC_JSON_TOP_LEVEL
} grpc_json_type;

#endif /* GRPC_CORE_LIB_JSON_JSON_COMMON_H */
