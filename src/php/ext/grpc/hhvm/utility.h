#include <cstdint>
#include <vector>

#include "grpc/grpc.h"
#include "grpc/slice.h"

// forward declarations
namespace HPHP
{
    struct Array;
}

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
    size_t length(void) const { return GRPC_SLICE_LENGTH(m_Slice); }
    const uint8_t* data(void) const { return GRPC_SLICE_START_PTR(m_Slice); }
    const grpc_slice& slice(void) const { return m_Slice; }

private:
    // interface functions
    grpc_slice& slice(void) { return m_Slice; }

    // member variables
    grpc_slice m_Slice;
};

// This class is an RAII wrapper around a metadata array
// during destruction
class MetadataArray
{
public:
    // constructors/descructors
    MetadataArray(void);
    ~MetadataArray(void);

    // interface functions
    bool init(const HPHP::Array& phpArray);
    grpc_metadata* const data(void) { return m_Array.metadata; }
    const grpc_metadata* const data(void) const { return m_Array.metadata; }
    size_t size(void) const { return m_Array.count; }
    const grpc_metadata_array& array(void) const { return m_Array; }

private:
    // interface functions
    grpc_metadata_array& array(void) { return m_Array; }

    // helper functions
    void destroyPHP(void);
    void resizeMetadata(const size_t capacity);

    // member variables
    grpc_metadata_array m_Array;
    std::vector<std::pair<Slice, Slice>> m_PHPData; // the key, value PHP Data
};



#endif /* NET_GRPC_HHVM_GRPC_UTILITY_H_