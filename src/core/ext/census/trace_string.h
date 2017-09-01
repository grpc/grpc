/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_EXT_CENSUS_TRACE_STRING_H
#define GRPC_CORE_EXT_CENSUS_TRACE_STRING_H

#include <grpc/slice.h>

/* String struct for tracing messages. Since this is a C API, we do not have
   access to a string class.  This is intended for use by higher level
   languages which wrap around the C API, as most of them have a string class.
   This will also be more efficient when copying, as we have an explicitly
   specified length.  Also, grpc_slice has reference counting which allows for
   interning. */
typedef struct trace_string {
  char *string;
  size_t length;
} trace_string;

#endif /* GRPC_CORE_EXT_CENSUS_TRACE_STRING_H */
