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

#ifndef GRPC_CORE_EXT_CENSUS_TRACE_LABEL_H
#define GRPC_CORE_EXT_CENSUS_TRACE_LABEL_H

#include "src/core/ext/census/trace_string.h"

/* Trace label (key/value pair) stores a label name and the label value. The
   value can be one of trace_string/int64_t/bool. */
typedef struct trace_label {
  trace_string key;
  enum label_type {
    /* Unknown value for debugging/error purposes */
    LABEL_UNKNOWN = 0,
    /* A string value */
    LABEL_STRING = 1,
    /* An integer value. */
    LABEL_INT = 2,
    /* A boolean value. */
    LABEL_BOOL = 3,
  } value_type;

  union value {
    trace_string label_str;
    int64_t label_int;
    bool label_bool;
  } value;
} trace_label;

#endif /* GRPC_CORE_EXT_CENSUS_TRACE_LABEL_H */
