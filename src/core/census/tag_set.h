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

#ifndef GRPC_INTERNAL_CORE_CENSUS_TAG_SET_H
#define GRPC_INTERNAL_CORE_CENSUS_TAG_SET_H

#include <grpc/census.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>

/* Encode to-be-propagated tags from a tag set into a memory buffer. The total
   number of bytes used in the buffer is returned. If the buffer is too small
   to contain the encoded tag set, then 0 is returned. */
size_t census_tag_set_encode_propagated(const census_tag_set *tags,
                                        char *buffer, size_t buf_size);

/* Encode to-be-propagated binary tags from a tag set into a memory
   buffer. The total number of bytes used in the buffer is returned. If the
   buffer is too small to contain the encoded tag set, then 0 is returned. */
size_t census_tag_set_encode_propagated_binary(const census_tag_set *tags,
                                               char *buffer, size_t buf_size);

/* Decode tag set buffers encoded with census_tag_set_encode_*(). */
census_tag_set *census_tag_set_decode(const char *buffer, size_t size,
                                      const char *bin_buffer, size_t bin_size);

#endif /* GRPC_INTERNAL_CORE_CENSUS_TAG_SET_H */
