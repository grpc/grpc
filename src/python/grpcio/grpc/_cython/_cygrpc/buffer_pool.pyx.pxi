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

import threading
import time
from cpython cimport array
import array

# Global buffer pool instance
cdef BufferPool _global_buffer_pool = BufferPool()

cdef class BufferPool:
    """
    Thread-safe buffer pool for reusing grpc_byte_buffer objects.
    
    This pool reduces memory allocation overhead by maintaining a cache of
    pre-allocated buffers for common message sizes.
    """
    
    cdef:
        # Simple Python dict for storing pools by size
        dict _pools
        # Maximum number of buffers to keep per size
        size_t _max_pool_size
        # Statistics for monitoring
        size_t _total_allocations
        size_t _total_reuses
        size_t _total_destructions
        # Thread safety
        object _lock
        
    def __cinit__(self, size_t max_pool_size=100):
        self._max_pool_size = max_pool_size
        self._total_allocations = 0
        self._total_reuses = 0
        self._total_destructions = 0
        self._pools = {}
        self._lock = threading.RLock()
    
    cdef grpc_byte_buffer* _get_buffer_from_pool(self, size_t size):
        """
        Get a buffer from the pool for the given size.
        Returns NULL if no buffer is available.
        """
        cdef:
            grpc_byte_buffer* buffer = NULL
            list pool_list
        
        if size in self._pools:
            pool_list = self._pools[size]
            if len(pool_list) > 0:
                buffer = pool_list.pop()
                self._total_reuses += 1
        
        return buffer
    
    cdef void _return_buffer_to_pool(self, grpc_byte_buffer* buffer, size_t size):
        """
        Return a buffer to the pool for reuse.
        """
        cdef list pool_list
        
        if size not in self._pools:
            self._pools[size] = []
        
        pool_list = self._pools[size]
        
        # Only add to pool if we haven't reached the maximum size
        if len(pool_list) < self._max_pool_size:
            pool_list.append(buffer)
        else:
            # Pool is full, destroy the buffer
            grpc_byte_buffer_destroy(buffer)
            self._total_destructions += 1
    
    cdef grpc_byte_buffer* get_buffer(self, bytes message) except *:
        """
        Get a buffer for the given message.
        Creates a new buffer if none is available in the pool.
        """
        cdef:
            size_t size = len(message)
            grpc_byte_buffer* buffer = NULL
            grpc_slice message_slice
        
        # Try to get from pool first
        buffer = self._get_buffer_from_pool(size)
        
        if buffer == NULL:
            # No buffer available in pool, create new one
            message_slice = grpc_slice_from_copied_buffer(
                <const char*>message, size)
            buffer = grpc_raw_byte_buffer_create(&message_slice, 1)
            grpc_slice_unref(message_slice)
            self._total_allocations += 1
        else:
            # Reuse buffer from pool - need to copy new data
            message_slice = grpc_slice_from_copied_buffer(
                <const char*>message, size)
            # Clear existing buffer and add new slice
            grpc_byte_buffer_destroy(buffer)
            buffer = grpc_raw_byte_buffer_create(&message_slice, 1)
            grpc_slice_unref(message_slice)
        
        return buffer
    
    cdef void return_buffer(self, grpc_byte_buffer* buffer, size_t size):
        """
        Return a buffer to the pool for reuse.
        """
        if buffer != NULL:
            self._return_buffer_to_pool(buffer, size)
    
    def get_stats(self):
        """
        Get pool statistics for monitoring.
        """
        cdef dict pool_sizes = {}
        for size, pool_list in self._pools.items():
            pool_sizes[size] = len(pool_list)
        
        return {
            'total_allocations': self._total_allocations,
            'total_reuses': self._total_reuses,
            'total_destructions': self._total_destructions,
            'reuse_rate': (self._total_reuses / max(1, self._total_allocations + self._total_reuses)) * 100,
            'pool_sizes': pool_sizes
        }
    
    def clear_pools(self):
        """
        Clear all pools, destroying all cached buffers.
        """
        cdef:
            list pool_list
            grpc_byte_buffer* buffer
        
        for pool_list in self._pools.values():
            for buffer in pool_list:
                grpc_byte_buffer_destroy(buffer)
                self._total_destructions += 1
        self._pools.clear()

def get_global_buffer_pool():
    """Get the global buffer pool instance."""
    return _global_buffer_pool 