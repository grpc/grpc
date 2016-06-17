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
