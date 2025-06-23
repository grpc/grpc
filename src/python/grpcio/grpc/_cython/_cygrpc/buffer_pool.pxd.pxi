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

from libcpp.queue cimport queue
from libcpp.unordered_map cimport unordered_map

cdef class BufferPool:
    """
    Thread-safe buffer pool for reusing grpc_byte_buffer objects.
    """
    
    cdef:
        unordered_map[size_t, queue[grpc_byte_buffer*]] _pools
        size_t _max_pool_size
        size_t _total_allocations
        size_t _total_reuses
        size_t _total_destructions
        object _lock
    
    cdef grpc_byte_buffer* _get_buffer_from_pool(self, size_t size) nogil
    cdef void _return_buffer_to_pool(self, grpc_byte_buffer* buffer, size_t size) nogil
    cdef grpc_byte_buffer* get_buffer(self, bytes message) except *
    cdef void return_buffer(self, grpc_byte_buffer* buffer, size_t size)
    def get_stats(self)
    def clear_pools(self)

cdef BufferPool _global_buffer_pool
def get_global_buffer_pool() 