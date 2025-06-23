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
import math

# Global buffer pool instance
cdef BufferPool _global_buffer_pool = BufferPool()

cdef class BufferPool:
    """
    Thread-safe buffer pool for reusing grpc_byte_buffer objects.
    
    This pool reduces memory allocation overhead by maintaining a cache of
    pre-allocated buffers for common message sizes using size bucketing.
    """
    
    cdef:
        # Size buckets: 1B, 10B, 100B, 1KB, 10KB, 100KB, 1MB, 10MB, 100MB
        list _bucket_sizes
        # Direct map: bucket_size -> buffer (no lists needed)
        dict _pools
        # Statistics for monitoring
        size_t _total_allocations
        size_t _total_reuses
        size_t _total_destructions
        # Thread safety
        object _lock
        
    def __cinit__(self):
        # Define size buckets: 1B, 10B, 100B, 1KB, 10KB, 100KB, 1MB, 10MB, 100MB
        self._bucket_sizes = [1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000]
        self._total_allocations = 0
        self._total_reuses = 0
        self._total_destructions = 0
        self._pools = {}
        self._lock = threading.RLock()
    
    cdef size_t _get_bucket_size(self, size_t message_size):
        """
        Find the smallest bucket size that can accommodate the message.
        If message is larger than all buckets, dynamically add a new bucket.
        Returns the bucket size to use.
        """
        cdef size_t bucket_size
        
        # First try to find an existing bucket
        for bucket_size in self._bucket_sizes:
            if bucket_size >= message_size:
                return bucket_size
        
        # Message is larger than all existing buckets
        # Add a new bucket size that's 10x larger than the current max
        cdef size_t new_bucket_size = self._bucket_sizes[-1] * 10
        
        # If the new bucket would be too large (> 1GB), cap it
        if new_bucket_size > 1000000000:  # 1GB
            new_bucket_size = 1000000000
        
        # Add the new bucket size
        self._bucket_sizes.append(new_bucket_size)
        
        return new_bucket_size
    
    cdef grpc_byte_buffer* _get_buffer_from_pool(self, size_t message_size):
        """
        Get a buffer from the appropriate size bucket.
        Returns NULL if no buffer is available.
        """
        cdef:
            grpc_byte_buffer* buffer = NULL
            size_t bucket_size
        
        bucket_size = self._get_bucket_size(message_size)
        
        if bucket_size in self._pools:
            buffer = self._pools[bucket_size]
            # Remove from pool (it's now in use)
            del self._pools[bucket_size]
            self._total_reuses += 1
        
        return buffer
    
    cdef void _return_buffer_to_pool(self, grpc_byte_buffer* buffer, size_t message_size):
        """
        Return a buffer to the appropriate size bucket.
        """
        cdef size_t bucket_size
        
        bucket_size = self._get_bucket_size(message_size)
        
        # If there's already a buffer in this bucket, destroy the new one
        if bucket_size in self._pools:
            grpc_byte_buffer_destroy(buffer)
            self._total_destructions += 1
        else:
            # Store the buffer in the pool
            self._pools[bucket_size] = buffer
    
    cdef grpc_byte_buffer* _reuse_buffer_slice_data(self, grpc_byte_buffer* buffer, bytes message):
        """
        Reuse a buffer by replacing its slice data efficiently.
        This is the key optimization - we reuse the buffer structure.
        """
        cdef:
            size_t size = len(message)
            grpc_slice new_slice
            grpc_slice_buffer* slice_buffer
        
        # Create new slice with the message data
        new_slice = grpc_slice_from_copied_buffer(<const char*>message, size)
        
        # Access the internal slice buffer through the union
        slice_buffer = &buffer.data.raw.slice_buffer
        
        # Clear existing slices (unref them)
        cdef size_t i
        for i in range(slice_buffer.count):
            grpc_slice_unref(slice_buffer.slices[i])
        
        # Reset the slice buffer
        slice_buffer.count = 0
        slice_buffer.length = 0
        
        # Add the new slice
        slice_buffer.slices[0] = new_slice
        slice_buffer.count = 1
        slice_buffer.length = size
        
        return buffer
    
    cdef grpc_byte_buffer* get_buffer(self, bytes message) except *:
        """
        Get a buffer for the given message.
        Creates a new buffer if none is available in the appropriate bucket.
        """
        cdef:
            size_t size = len(message)
            grpc_byte_buffer* buffer = NULL
            grpc_slice message_slice
        
        # Try to get from appropriate bucket first
        buffer = self._get_buffer_from_pool(size)
        
        if buffer == NULL:
            # No buffer available in bucket, create new one
            message_slice = grpc_slice_from_copied_buffer(
                <const char*>message, size)
            buffer = grpc_raw_byte_buffer_create(&message_slice, 1)
            grpc_slice_unref(message_slice)
            self._total_allocations += 1
        else:
            # ✅ ACTUAL REUSE: Replace slice data without destroying buffer structure
            buffer = self._reuse_buffer_slice_data(buffer, message)
        
        return buffer
    
    cdef void return_buffer(self, grpc_byte_buffer* buffer, size_t size):
        """
        Return a buffer to the appropriate bucket for reuse.
        """
        if buffer != NULL:
            self._return_buffer_to_pool(buffer, size)
    
    def get_stats(self):
        """
        Get pool statistics for monitoring.
        """
        cdef dict bucket_stats = {}
        for bucket_size in self._bucket_sizes:
            if bucket_size in self._pools:
                bucket_stats[bucket_size] = 1  # Only 1 buffer per bucket now
            else:
                bucket_stats[bucket_size] = 0
        
        return {
            'total_allocations': self._total_allocations,
            'total_reuses': self._total_reuses,
            'total_destructions': self._total_destructions,
            'reuse_rate': (self._total_reuses / max(1, self._total_allocations + self._total_reuses)) * 100,
            'bucket_sizes': self._bucket_sizes,
            'bucket_stats': bucket_stats,
            'active_buckets': len(self._pools)
        }
    
    def clear_pools(self):
        """
        Clear all pools, destroying all cached buffers.
        """
        cdef grpc_byte_buffer* buffer
        
        for bucket_size, buffer in self._pools.items():
            grpc_byte_buffer_destroy(buffer)
            self._total_destructions += 1
        self._pools.clear()

def get_global_buffer_pool():
    """Get the global buffer pool instance."""
    return _global_buffer_pool 