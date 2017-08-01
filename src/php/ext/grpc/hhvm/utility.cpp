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

#include "hphp/runtime/base/array-iterator.h"
#include "hphp/runtime/base/memory-manager.h"

#include "grpc/byte_buffer_reader.h"
#include "grpc/support/alloc.h"

#include "utility.h"

/*****************************************************************************/
/*                                   Slice                                   */
/*****************************************************************************/

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
    gpr_slice_unref(m_Slice);
}

Slice::Slice(const Slice& otherSlice)
{
    m_Slice = otherSlice.m_Slice;
    gpr_slice_ref(m_Slice);
}

Slice::Slice(Slice&& otherSlice)
{
    // emulate move as copy
    m_Slice = otherSlice.m_Slice;
    gpr_slice_ref(m_Slice);
}

Slice& Slice::operator=(const Slice& rhsSlice)
{
    if (this != &rhsSlice)
    {
        m_Slice = rhsSlice.m_Slice;
        gpr_slice_ref(m_Slice);
    }
    return *this;
}

grpc_byte_buffer* const Slice::byteBuffer(void) const
{
    grpc_slice* const pSlice{ const_cast<grpc_slice*>(&m_Slice) };
    return const_cast<grpc_byte_buffer* const>(grpc_raw_byte_buffer_create(pSlice, 1));
}

void Slice::increaseRef(void)
{
    gpr_slice_ref(m_Slice);
}

/*****************************************************************************/
/*                               MetadataArray                               */
/*****************************************************************************/

MetadataArray::MetadataArray(const bool ownPHP) : m_OwnPHP{ ownPHP },
    m_PHPData{}
{
    grpc_metadata_array_init(&m_Array);
}

MetadataArray::~MetadataArray(void)
{
    m_OwnPHP ? destroyPHP() : freePHP();
    grpc_metadata_array_destroy(&m_Array);
}

// Populates a grpc_metadata_array with the data in a PHP array object.
// Returns true on success and false on failure
bool MetadataArray::init(const HPHP::Array& phpArray, const bool ownPHP)
{
    // destroy/free any PHP data
    m_OwnPHP ? destroyPHP() : freePHP();
    m_OwnPHP = ownPHP;

    // precheck validity of data
    size_t count{ 0 };
    for (HPHP::ArrayIter iter{ phpArray }; iter; ++iter)
    {
        HPHP::Variant key{ iter.first() };
        if (key.isNull() || !key.isString() ||
            !grpc_header_key_is_legal(grpc_slice_from_static_string(key.toString().c_str())))
        {
            return false;
        }

        HPHP::Variant value{ iter.second() };
        if (value.isNull() || !value.isArray())
        {
            return false;
        }

        HPHP::Array innerArray{ value.toArray() };
        for (HPHP::ArrayIter iter2(innerArray); iter2; ++iter2, ++count)
        {
            HPHP::Variant value2{ iter2.second() };
            if (value2.isNull() || !value2.isString())
            {
                return false;
            }
        }
    }
    if (count > m_Array.capacity) resizeMetadata(count);

    // create metadata array
    size_t elem{ 0 };
    for (HPHP::ArrayIter iter(phpArray); iter; ++iter)
    {
        HPHP::Variant key{ iter.first() };
        HPHP::Variant value{ iter.second() };
        HPHP::Array innerArray{ value.toArray() };
        for (HPHP::ArrayIter iter2(innerArray); iter2; ++iter2, ++elem)
        {
            HPHP::Variant value2{ iter2.second() };

            Slice keySlice{ key.toString().c_str() };
            Slice valueSlice{ value2.toString().c_str() };
            m_PHPData.emplace_back(keySlice, valueSlice);

            m_Array.metadata[elem].key = m_PHPData.back().first.slice();
            m_Array.metadata[elem].value = m_PHPData.back().second.slice();

            std::cout << m_PHPData.back().first.data() << ' ' << m_PHPData.back().second.data() << std::endl;
        }
        std::cout << GRPC_SLICE_START_PTR(m_Array.metadata[elem-1].key) << ' '
                  << GRPC_SLICE_START_PTR(m_Array.metadata[elem-1].value) << std::endl;
    }
    m_Array.count = count;
    std::cout << "Count: " << count << std::endl;
    return true;
}

// Creates and returns a PHP array object with the data in a
// grpc_metadata_array. Returns NULL on failure
HPHP::Variant MetadataArray::phpData(void) const
{
    HPHP::Array phpArray{ HPHP::Array::Create() };
    for(size_t elem{ 0 }; elem < m_Array.count; ++elem)
    {
        const grpc_metadata& element(m_Array.metadata[elem]);

        HPHP::String key{ reinterpret_cast<const char* const>(GRPC_SLICE_START_PTR(element.key)),
                          GRPC_SLICE_LENGTH(element.key), HPHP::CopyString };
        HPHP::String value{ reinterpret_cast<const char* const>(GRPC_SLICE_START_PTR(element.value)),
                            GRPC_SLICE_LENGTH(element.value), HPHP::CopyString };

        if (!phpArray.exists(key, true))
        {
            phpArray.set(key, HPHP::Array::Create(), true);
        }

        HPHP::Variant current{ phpArray[key] };
        if (current.isNull() || !current.isArray())
        {
            HPHP::SystemLib::throwInvalidArgumentExceptionObject("Metadata hash somehow contains wrong types.");
            return HPHP::Variant{};
        }
        HPHP::Array currentArray{ current.toArray() };
        currentArray.append(HPHP::Variant{ value });
    }

    return HPHP::Variant(phpArray);
}

void MetadataArray::destroyPHP(void)
{
    // destroy PHP data
    m_PHPData.clear();
    m_Array.count = 0;
}

void MetadataArray::freePHP(void)
{
    // free the PHP data by increasinf ref counts
    for(std::pair<Slice, Slice>& phpData : m_PHPData)
    {
        phpData.first.increaseRef();
        phpData.second.increaseRef();
    }

    destroyPHP();
}

void MetadataArray::resizeMetadata(const size_t capacity)
{
    if (capacity > m_Array.capacity)
    {
        // allocate new memory
        grpc_metadata* const pMetadataNew{ reinterpret_cast<grpc_metadata*>(gpr_malloc(capacity * sizeof(grpc_metadata))) };

        // move existing items
        for(size_t elem{ 0 }; elem < m_Array.count; ++elem)
        {
            pMetadataNew[elem] = m_Array.metadata[elem];
        }

        // destroy old memory
        gpr_free(m_Array.metadata);
        m_Array.metadata = pMetadataNew;
        m_Array.capacity = capacity;
    }
}
