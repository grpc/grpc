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

#include "utility.h"

#include "grpc/byte_buffer_reader.h"

Slice::Slice(const char* const string) :
    m_Slice{ grpc_slice_from_copied_string(string) }
{
}

Slice::Slice(const char* const string, const size_t length) :
    m_Slice{ grpc_slice_from_copied_buffer(string, length) }
{
}

Slice::Slice(grpc_byte_buffer_reader& reader) :
    m_Slice{ grpc_byte_buffer_reader_readall(&reader) }
{
}

Slice::~Slice(void)
{
    grpc_slice_unref(m_Slice);
}
