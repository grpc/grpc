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

#include <utility>

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "slice.h"
#include "common.h"

#include "grpc/byte_buffer_reader.h"
#include "grpc/support/alloc.h"

/*****************************************************************************/
/*                                   Slice                                   */
/*****************************************************************************/

Slice::Slice(HPHP::String string)
{
  size_t length { static_cast<size_t>(string.size()) };
  m_Slice = grpc_slice_from_copied_buffer(string.c_str(), length);
}

Slice::Slice(const char* const string)
{
    m_Slice = string ? grpc_slice_from_copied_string(string) :
                       grpc_empty_slice();
}

Slice::Slice(const char* const string, const size_t length)
{
    m_Slice = string ? grpc_slice_from_copied_buffer(string, length) :
                       grpc_empty_slice();
}

Slice::Slice(const grpc_byte_buffer* const buffer)
{
    grpc_byte_buffer_reader reader;
    if (!buffer || !grpc_byte_buffer_reader_init(&reader, const_cast<grpc_byte_buffer*>(buffer)))
    {
        m_Slice = grpc_empty_slice();
    }
    else
    {
        m_Slice = grpc_byte_buffer_reader_readall(&reader);
    }
}

Slice::~Slice(void)
{
    destroy();
}

Slice::Slice(const Slice& otherSlice)
{
    m_Slice = otherSlice.m_Slice;
    increaseRef();
}

Slice::Slice(Slice&& otherSlice)
{
    // move is emulated as copy since when otherSlice goes out of scope
    // it will decrease its refcount but now this slice will hold it
    m_Slice = otherSlice.m_Slice;
    increaseRef();
}

Slice& Slice::operator=(const Slice& rhsSlice)
{
    if (this != &rhsSlice)
    {
        // destroy existing slice
        destroy();

        // copy other slice via increasing refcount
        m_Slice = rhsSlice.m_Slice;
        increaseRef();
    }
    return *this;
}

Slice& Slice::operator=(Slice&& rhsSlice)
{
    if (this != &rhsSlice)
    {
        // swap these
        std::swap(m_Slice, rhsSlice.m_Slice);
    }
    return *this;
}

// Note: Just casting this to a "char *" can read past
// the end of the string and into other memory
const uint8_t* const Slice::data(void) const
{
    static char* emptyString{""};

    return (empty() ? reinterpret_cast<const uint8_t*>(emptyString) :
                      GRPC_SLICE_START_PTR(m_Slice));
}

grpc_byte_buffer* const Slice::byteBuffer(void) const
{
    grpc_slice* const pSlice{ const_cast<grpc_slice*>(&m_Slice) };
    return const_cast<grpc_byte_buffer* const>(grpc_raw_byte_buffer_create(pSlice, 1));
}

HPHP::String Slice::string(void)
{
  return HPHP::String{ reinterpret_cast<const char*>(data()), length(), HPHP::CopyString };
}

// Make an explicit copy of c strings
char *Slice::c_str(void)
{
    const char *currentData { reinterpret_cast<const char *>(data()) };
    size_t cStrLen { length() };
    char *cStr;

    if (currentData[cStrLen-1] != '\0')
    {
        // Append on a null byte
        cStr = reinterpret_cast<char *>(gpr_zalloc(cStrLen + 1));
    } else {
        cStr = reinterpret_cast<char *>(gpr_zalloc(cStrLen));
    }

    std::memcpy(reinterpret_cast<void*>(cStr),
                reinterpret_cast<const void*>(currentData), cStrLen);

    return cStr;
}

void Slice::destroy(void)
{
    gpr_slice_unref(m_Slice);
}

void Slice::increaseRef(void)
{
    gpr_slice_ref(m_Slice);
}
