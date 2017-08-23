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

#ifndef NET_GRPC_HHVM_GRPC_SLICE_H_
#define NET_GRPC_HHVM_GRPC_SLICE_H_

#include <cstdint>

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/base/string-util.h"

#include "grpc/grpc.h"
#include "grpc/slice.h"

// This class is an RAII wrapper around a slice
class Slice
{
public:
    // constructors/descructors
    Slice(void) : m_Slice( grpc_empty_slice() ) {}
    Slice(const HPHP::String& string);
    Slice(const char* const string);
    Slice(const char* const string, const size_t length);
    Slice(const grpc_byte_buffer* const buffer);
    ~Slice(void);
    Slice(const Slice& otherSlice);
    Slice(Slice&& otherSlice);
    Slice& operator=(const Slice& rhsSlice);
    Slice& operator=(Slice&& rhsSlice);

    // interface functions
    size_t length(void) const { return GRPC_SLICE_LENGTH(m_Slice); }
    bool empty(void) const { return (length() == 0); }
    const uint8_t* const data(void) const;
    const grpc_slice& slice(void) const { return m_Slice; }
    grpc_slice& slice(void) { return m_Slice; } // several methods need non const &
    HPHP::String string(void);
    char* const c_str(void) const;
    void increaseRef(void);
    grpc_byte_buffer* const byteBuffer(void) const;

private:
    // helper functions
    void destroy(void);

    // member variables
    grpc_slice m_Slice;
};


#endif // NET_GRPC_HHVM_GRPC_SLICE_H_
