#!/usr/bin/env python3
"""
Test script for the fixed buffer pool implementation.
This verifies that buffers are actually being reused properly.
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from grpc._cython._cygrpc import get_global_buffer_pool

def test_buffer_pool_reuse():
    """Test that buffers are actually reused."""
    pool = get_global_buffer_pool()
    
    # Test with a small message
    message1 = b"Hello, World!"
    message2 = b"Goodbye, World!"
    
    print("Testing buffer pool reuse...")
    print(f"Initial stats: {pool.get_stats()}")
    
    # Get first buffer
    buffer1 = pool.get_buffer(message1)
    print(f"After first allocation: {pool.get_stats()}")
    
    # Return first buffer
    pool.return_buffer(buffer1, len(message1))
    print(f"After returning buffer: {pool.get_stats()}")
    
    # Get second buffer - should reuse the first one
    buffer2 = pool.get_buffer(message2)
    print(f"After second allocation: {pool.get_stats()}")
    
    # Verify the buffer was reused
    stats = pool.get_stats()
    if stats['total_reuses'] > 0:
        print("✅ SUCCESS: Buffer was reused!")
        print(f"Reuse rate: {stats['reuse_rate']:.1f}%")
    else:
        print("❌ FAILURE: Buffer was not reused")
    
    # Clean up
    pool.return_buffer(buffer2, len(message2))
    pool.clear_pools()

def test_performance_improvement():
    """Test performance improvement with repeated allocations."""
    pool = get_global_buffer_pool()
    
    # Test with repeated allocations of the same size
    message = b"Test message for performance testing"
    size = len(message)
    
    print("\nTesting performance improvement...")
    print(f"Allocating 1000 buffers of size {size}...")
    
    import time
    start_time = time.time()
    
    buffers = []
    for i in range(1000):
        buffer = pool.get_buffer(message)
        buffers.append(buffer)
    
    allocation_time = time.time() - start_time
    
    # Return all buffers
    start_time = time.time()
    for buffer in buffers:
        pool.return_buffer(buffer, size)
    
    return_time = time.time() - start_time
    
    # Get second batch - should reuse buffers
    start_time = time.time()
    buffers2 = []
    for i in range(1000):
        buffer = pool.get_buffer(message)
        buffers2.append(buffer)
    
    reuse_time = time.time() - start_time
    
    stats = pool.get_stats()
    
    print(f"First allocation batch: {allocation_time:.4f}s")
    print(f"Return time: {return_time:.4f}s")
    print(f"Reuse allocation batch: {reuse_time:.4f}s")
    print(f"Speedup: {allocation_time/reuse_time:.2f}x")
    print(f"Total reuses: {stats['total_reuses']}")
    print(f"Reuse rate: {stats['reuse_rate']:.1f}%")
    
    # Clean up
    for buffer in buffers2:
        pool.return_buffer(buffer, size)
    pool.clear_pools()

if __name__ == "__main__":
    test_buffer_pool_reuse()
    test_performance_improvement()
    print("\nBuffer pool test completed!") 