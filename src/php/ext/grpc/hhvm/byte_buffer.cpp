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

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include <string.h>

#include "byte_buffer.h"

#include "hphp/runtime/base/memory-manager.h"

#include "grpc/grpc.h"
#include "grpc/byte_buffer_reader.h"

#include "utility.h"

grpc_byte_buffer* string_to_byte_buffer(const char *string, const size_t length)
{
    Slice slice{ string, length };
    grpc_byte_buffer* buffer{ grpc_raw_byte_buffer_create(&slice.slice(), 1) };
    return buffer;
}

void byte_buffer_to_string(grpc_byte_buffer* const buffer, char** const out_string,
                           size_t* const out_length)
{
    grpc_byte_buffer_reader reader;
    if (buffer == nullptr || !grpc_byte_buffer_reader_init(&reader, buffer))
    {
        /* TODO(dgq): distinguish between the error cases. */
        *out_string = nullptr;
        *out_length = 0;
        return;
    }

    Slice slice{ reader };
    // acquire HPHP zeroed memory to hold string plus null terminator
    size_t length{ slice.length() };
    char* const string{ reinterpret_cast<char*>(HPHP::req::calloc(length + 1, sizeof(char))) };
    std::memcpy(string, reinterpret_cast<const void*>(slice.data()), length);

    *out_string = string;
    *out_length = length;
}
