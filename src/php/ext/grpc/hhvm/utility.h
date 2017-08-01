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
#include <iostream>
#include <iomanip>
#include <vector>

#include "hphp/runtime/ext/extension.h"

#include "grpc/grpc.h"
#include "grpc/slice.h"

class TraceScope
{
public:
    TraceScope(const std::string& message, const std::string& function, const std::string& file) :
        m_Message{ message}, m_Function{ function }, m_File{ file }
    {
        std::cout << __TIME__ << " - " << m_Message << " - Entry " << m_Function << ' '
                  << m_File << std::endl;
    }
    ~TraceScope(void)
    {
        std::cout << __TIME__ << " - " << m_Message << " Exit " << m_Function << ' '
                  << m_File << std::endl;
    }
    std::string m_Message;
    std::string m_Function;
    std::string m_File;
};

#define HHVM_TRACE_DEBUG

#ifdef HHVM_TRACE_DEBUG
    #define HHVM_TRACE_SCOPE(x) TraceScope traceScope{ x, __func__, __FILE__ };
#else
    #define HHVM_TRACE_SCOPE(x)
#endif // HHVM_TRACE_DEBUG

// This class is an RAII wrapper around a slice
class Slice
{
public:
    // constructors/descructors
    Slice(void) : m_Slice{ grpc_empty_slice() } {}
    Slice(const char* const string);
    Slice(const char* const string, const size_t length);
    Slice(const grpc_byte_buffer* const buffer);
    ~Slice(void);
    Slice(const Slice& otherSlice);
    Slice(Slice&& otherSlice);
    Slice& operator=(const Slice& rhsSlice);
    Slice& operator=(Slice&& rhsSlice) = delete;

    // interface functions
    size_t length(void) const { return GRPC_SLICE_LENGTH(m_Slice); }
    const uint8_t* data(void) const { return GRPC_SLICE_START_PTR(m_Slice); }
    const grpc_slice& slice(void) const { return m_Slice; }
    grpc_slice& slice(void) { return m_Slice; } // several methods need non const &
    void increaseRef(void);
    grpc_byte_buffer* const byteBuffer(void) const;

private:
    // member variables
    grpc_slice m_Slice;
};


#endif // NET_GRPC_HHVM_GRPC_UTILITY_H_
