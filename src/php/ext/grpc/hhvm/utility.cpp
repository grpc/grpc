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
#include "grpc/support/alloc.h"

#include "hphp/runtime/ext/extension.h"

/*****************************************************************************/
/*                                  Slice                                    */
/*****************************************************************************/

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

/*****************************************************************************/
/*                              MetadataArray                                */
/*****************************************************************************/

MetadataArray::MetadataArray(void) : m_Array{ grpc_metadata_array() }
{
    grpc_metadata_array_init(&m_Array);
}

MetadataArray::~MetadataArray(void)
{
    destroy();
}

bool MetadataArray::init(const HPHP::Array& phpArray)
{
    // destroy any existing data
    destroy();

    // find number of elements to store and precheck for types
    size_t elements{ 0 };
    for (HPHP::ArrayIter iter{ phpArray }; iter; ++iter) {
        HPHP::Variant key{ iter.first() };
        if (key.isNull() || !key.isString()) {
            destroy();
            return false;
        }

        HPHP::Variant value{ iter.second() };
        if (value.isNull() || value.isArray()) {
            destroy();
            return false;
        }

        elements += value.toArray().size();
    }

    // allocate enough size to hold info
    m_Array.metadata = reinterpret_cast<grpc_metadata*>(gpr_malloc(elements * sizeof(grpc_metadata)));
    m_Array.capacity = elements;
    m_Array.count = elements;

    size_t count{ 0 };
    for (HPHP::ArrayIter iter{ phpArray }; iter; ++iter) {
        HPHP::Variant key{ iter.first() };
        if (!grpc_header_key_is_legal(grpc_slice_from_static_string(key.toString().c_str())))
        {
            destroy();
            return false;
        }

        HPHP::Variant value{ iter.second() };

        HPHP::Array innerArray{ value.toArray() };
        for (HPHP::ArrayIter iter2{ innerArray }; iter2; ++iter2, ++count) {
            HPHP::Variant key2{ iter2.first() };
            HPHP::Variant value2{ iter2.second() };
            if (value2.isNull() || !value2.isString()) {
                destroy();
                return false;
            }

            // convert key/value pair to slices
            Slice keySlice{ key.toString().c_str() };
            HPHP::String value2String{ value2.toString() };
            Slice valueSlice{ value2String.c_str(), static_cast<size_t>(value2String.length()) };

            // copying the slice does not increase the reference count
            m_Array.metadata[count].key = keySlice.slice();
            m_Array.metadata[count].value = valueSlice.slice();

            m_PHPData.emplace_back(std::move(keySlice), std::move(valueSlice));
        }
    }

    return true;
}

void MetadataArray::destroy(void)
{
    // destroy data in the PHP array with auto destruction of the slices
    m_PHPData.clear();
    grpc_metadata_array_destroy(&m_Array);
    m_Array.count = 0;
    m_Array.capacity = 0;
}
