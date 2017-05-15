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

#ifndef GRPC_CORE_EXT_FILTERS_HTTP_MESSAGE_COMPRESS_MESSAGE_COMPRESS_FILTER_H
#define GRPC_CORE_EXT_FILTERS_HTTP_MESSAGE_COMPRESS_MESSAGE_COMPRESS_FILTER_H

#include <grpc/impl/codegen/compression_types.h>

#include "src/core/lib/channel/channel_stack.h"

/** Compression filter for outgoing data.
 *
 * See <grpc/compression.h> for the available compression settings.
 *
 * Compression settings may come from:
 *  - Channel configuration, as established at channel creation time.
 *  - The metadata accompanying the outgoing data to be compressed. This is
 *    taken as a request only. We may choose not to honor it. The metadata key
 *    is given by \a GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY.
 *
 * Compression can be disabled for concrete messages (for instance in order to
 * prevent CRIME/BEAST type attacks) by having the GRPC_WRITE_NO_COMPRESS set in
 * the BEGIN_MESSAGE flags.
 *
 * The attempted compression mechanism is added to the resulting initial
 * metadata under the'grpc-encoding' key.
 *
 * If compression is actually performed, BEGIN_MESSAGE's flag is modified to
 * incorporate GRPC_WRITE_INTERNAL_COMPRESS. Otherwise, and regardless of the
 * aforementioned 'grpc-encoding' metadata value, data will pass through
 * uncompressed. */

extern const grpc_channel_filter grpc_message_compress_filter;

#endif /* GRPC_CORE_EXT_FILTERS_HTTP_MESSAGE_COMPRESS_MESSAGE_COMPRESS_FILTER_H \
          */
