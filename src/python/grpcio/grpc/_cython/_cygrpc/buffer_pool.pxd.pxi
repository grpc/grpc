# Copyright 2024 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cdef extern from "grpc/byte_buffer.h":
    ctypedef enum grpc_byte_buffer_type:
        GRPC_BB_RAW
    
    ctypedef struct grpc_byte_buffer:
        void* reserved
        grpc_byte_buffer_type type
        union grpc_byte_buffer_data:
            struct reserved:
                void* reserved[8]
            struct raw:
                int compression  # grpc_compression_algorithm
                grpc_slice_buffer slice_buffer
    
    ctypedef struct grpc_slice:
        pass
    
    ctypedef struct grpc_slice_buffer:
        grpc_slice* slices
        size_t count
        size_t length
    
    grpc_byte_buffer* grpc_raw_byte_buffer_create(grpc_slice* slices, size_t nslices)
    void grpc_byte_buffer_destroy(grpc_byte_buffer* bb)
    size_t grpc_byte_buffer_length(grpc_byte_buffer* bb)

cdef extern from "grpc/slice.h":
    grpc_slice grpc_slice_from_copied_buffer(const char* source, size_t len)
    void grpc_slice_unref(grpc_slice slice)

cdef class BufferPool:
    """
    Thread-safe buffer pool for reusing grpc_byte_buffer objects.
    """
    
    cdef:
        list _bucket_sizes
        dict _pools
        size_t _total_allocations
        size_t _total_reuses
        size_t _total_destructions
        object _lock
    
    cdef size_t _get_bucket_size(self, size_t message_size)
    cdef grpc_byte_buffer* _get_buffer_from_pool(self, size_t message_size)
    cdef void _return_buffer_to_pool(self, grpc_byte_buffer* buffer, size_t message_size)
    cdef grpc_byte_buffer* _reuse_buffer_slice_data(self, grpc_byte_buffer* buffer, bytes message)
    cdef grpc_byte_buffer* get_buffer(self, bytes message) except *
    cdef void return_buffer(self, grpc_byte_buffer* buffer, size_t size)
    def get_stats(self)
    def clear_pools(self)

cdef BufferPool get_global_buffer_pool() 