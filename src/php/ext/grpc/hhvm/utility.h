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
#include <vector>

#include "hphp/runtime/ext/extension.h"

#include "grpc/grpc.h"
#include "grpc/slice.h"

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

// This class is an RAII wrapper around a metadata array
class MetadataArray
{
public:
    // constructors/descructors
    MetadataArray(const bool ownPHP = true);
    ~MetadataArray(void);

    // interface functions
    bool init(const HPHP::Array& phpArray);
    grpc_metadata* const data(void) { return m_Array.metadata; }
    const grpc_metadata* const data(void) const { return m_Array.metadata; }
    size_t size(void) const { return m_Array.count; }
    const grpc_metadata_array& array(void) const { return m_Array; } //
    grpc_metadata_array& array(void) { return m_Array; } // several methods need non const &
    bool init(const HPHP::Array& phpArray, const bool ownPHP = true);
    HPHP::Variant phpData(void) const;

private:
    // helper functions
    void destroyPHP(void);
    void freePHP(void);
    void resizeMetadata(const size_t capacity);

    // member variables
    bool m_OwnPHP;
    grpc_metadata_array m_Array;
    std::vector<std::pair<Slice, Slice>> m_PHPData; // the key, value PHP Data
};

#endif // NET_GRPC_HHVM_GRPC_UTILITY_H_
