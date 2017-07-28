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

#ifndef NET_GRPC_HHVM_GRPC_UTILITY_H_
#define NET_GRPC_HHVM_GRPC_UTILITY_H_

#include <cstdint>

#include "grpc/grpc.h"
#include "grpc/slice.h"

// This class is an RAII wrapper around a slice that will do automatic unref
// during destruction
class Slice
{
public:
    // constructors/descructors
    Slice(void) : m_Slice{ grpc_empty_slice() } {}
    Slice(const char* const string);
    Slice(const char* const string, const size_t length);
    Slice(grpc_byte_buffer_reader& reader);
    ~Slice(void);

    // interface functions
    grpc_slice& slice(void) { return m_Slice; };
    const grpc_slice& slice(void) const { return m_Slice; }
    size_t length(void) const { return GRPC_SLICE_LENGTH(m_Slice); }
    const uint8_t* data(void) const { return GRPC_SLICE_START_PTR(m_Slice); }

private:
    // member variables
    grpc_slice m_Slice;
};


#endif /* NET_GRPC_HHVM_GRPC_UTILITY_H_ */
