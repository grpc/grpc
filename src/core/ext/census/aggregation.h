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

#include <stddef.h>

#ifndef GRPC_CORE_EXT_CENSUS_AGGREGATION_H
#define GRPC_CORE_EXT_CENSUS_AGGREGATION_H

/** Structure used to describe an aggregation type. */
struct census_aggregation_ops {
  /* Create a new aggregation. The pointer returned can be used in future calls
     to clone(), free(), record(), data() and reset(). */
  void *(*create)(const void *create_arg);
  /* Make a copy of an aggregation created by create() */
  void *(*clone)(const void *aggregation);
  /* Destroy an aggregation created by create() */
  void (*free)(void *aggregation);
  /* Record a new value against aggregation. */
  void (*record)(void *aggregation, double value);
  /* Return current aggregation data. The caller must cast this object into
     the correct type for the aggregation result. The object returned can be
     freed by using free_data(). */
  void *(*data)(const void *aggregation);
  /* free data returned by data() */
  void (*free_data)(void *data);
  /* Reset an aggregation to default (zero) values. */
  void (*reset)(void *aggregation);
  /* Merge 'from' aggregation into 'to'. Both aggregations must be compatible */
  void (*merge)(void *to, const void *from);
  /* Fill buffer with printable string version of aggregation contents. For
     debugging only. Returns the number of bytes added to buffer (a value == n
     implies the buffer was of insufficient size). */
  size_t (*print)(const void *aggregation, char *buffer, size_t n);
};

#endif /* GRPC_CORE_EXT_CENSUS_AGGREGATION_H */
