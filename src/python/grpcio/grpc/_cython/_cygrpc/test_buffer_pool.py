#!/usr/bin/env python3
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

"""
Test script to verify buffer pool implementation works correctly.
"""

import threading
import time
from typing import List

# Import the buffer pool
from .buffer_pool import get_global_buffer_pool, BufferPool


def test_basic_functionality():
    """Test basic buffer pool functionality."""
    print("Testing basic buffer pool functionality...")
    
    pool = get_global_buffer_pool()
    
    # Test with different message sizes
    test_messages = [
        b"Hello, World!",
        b"x" * 1024,
        b"y" * 4096,
        b"z" * 16384
    ]
    
    for message in test_messages:
        # Get buffer from pool
        buffer = pool.get_buffer(message)
        assert buffer is not None, f"Failed to get buffer for message size {len(message)}"
        
        # Return buffer to pool
        pool.return_buffer(buffer, len(message))
        
        print(f"  ✓ Successfully handled message size {len(message)}")
    
    print("  ✓ Basic functionality test passed")


def test_reuse_functionality():
    """Test that buffers are actually reused."""
    print("Testing buffer reuse functionality...")
    
    pool = get_global_buffer_pool()
    
    # Clear any existing pools
    pool.clear_pools()
    
    message = b"x" * 1024
    message_size = len(message)
    
    # First allocation should create new buffer
    buffer1 = pool.get_buffer(message)
    stats1 = pool.get_stats()
    assert stats1['total_allocations'] == 1, "First allocation should increment total_allocations"
    assert stats1['total_reuses'] == 0, "First allocation should not reuse"
    
    # Return buffer to pool
    pool.return_buffer(buffer1, message_size)
    
    # Second allocation should reuse buffer
    buffer2 = pool.get_buffer(message)
    stats2 = pool.get_stats()
    assert stats2['total_allocations'] == 1, "Second allocation should not increment total_allocations"
    assert stats2['total_reuses'] == 1, "Second allocation should reuse"
    
    # Return buffer again
    pool.return_buffer(buffer2, message_size)
    
    print("  ✓ Buffer reuse test passed")


def test_thread_safety():
    """Test that buffer pool is thread-safe."""
    print("Testing thread safety...")
    
    pool = get_global_buffer_pool()
    pool.clear_pools()
    
    message = b"x" * 1024
    message_size = len(message)
    num_threads = 10
    iterations_per_thread = 100
    
    def worker_thread():
        """Worker thread function."""
        for _ in range(iterations_per_thread):
            buffer = pool.get_buffer(message)
            pool.return_buffer(buffer, message_size)
    
    # Start multiple threads
    threads = []
    for _ in range(num_threads):
        thread = threading.Thread(target=worker_thread)
        threads.append(thread)
        thread.start()
    
    # Wait for all threads to complete
    for thread in threads:
        thread.join()
    
    # Check final statistics
    stats = pool.get_stats()
    expected_operations = num_threads * iterations_per_thread
    
    print(f"  Total operations: {stats['total_allocations'] + stats['total_reuses']}")
    print(f"  Expected operations: {expected_operations}")
    print(f"  Reuse rate: {stats['reuse_rate']:.1f}%")
    
    assert (stats['total_allocations'] + stats['total_reuses']) == expected_operations, \
        "Total operations should match expected count"
    
    print("  ✓ Thread safety test passed")


def test_pool_size_limits():
    """Test that pool size limits are respected."""
    print("Testing pool size limits...")
    
    pool = BufferPool(max_pool_size=2)  # Small pool for testing
    pool.clear_pools()
    
    message = b"x" * 1024
    message_size = len(message)
    
    # Allocate and return 5 buffers (more than pool size)
    buffers = []
    for _ in range(5):
        buffer = pool.get_buffer(message)
        buffers.append(buffer)
    
    # Return all buffers
    for buffer in buffers:
        pool.return_buffer(buffer, message_size)
    
    # Check statistics
    stats = pool.get_stats()
    assert stats['total_allocations'] == 5, "Should have allocated 5 buffers"
    assert stats['total_reuses'] == 0, "Should not have reused any buffers yet"
    assert stats['total_destructions'] == 3, "Should have destroyed 3 buffers (5 - 2 pool size)"
    
    # Check pool size
    pool_sizes = stats['pool_sizes']
    assert message_size in pool_sizes, "Message size should be in pool"
    assert pool_sizes[message_size] == 2, "Pool should contain exactly 2 buffers"
    
    print("  ✓ Pool size limits test passed")


def test_performance_improvement():
    """Test that buffer pool provides performance improvement."""
    print("Testing performance improvement...")
    
    pool = get_global_buffer_pool()
    pool.clear_pools()
    
    message = b"x" * 1024
    message_size = len(message)
    iterations = 1000
    
    # Test without pooling (simulate old behavior)
    start_time = time.perf_counter()
    for _ in range(iterations):
        # Simulate direct allocation
        import grpc
        from grpc._cython import cygrpc
        operation = cygrpc.SendMessageOperation(message, 0)
        operation.c()
        operation.un_c()
    direct_time = time.perf_counter() - start_time
    
    # Test with pooling
    start_time = time.perf_counter()
    for _ in range(iterations):
        buffer = pool.get_buffer(message)
        pool.return_buffer(buffer, message_size)
    pool_time = time.perf_counter() - start_time
    
    improvement = ((direct_time - pool_time) / direct_time) * 100
    
    print(f"  Direct allocation time: {direct_time:.4f} seconds")
    print(f"  Buffer pool time: {pool_time:.4f} seconds")
    print(f"  Improvement: {improvement:.1f}%")
    
    # Check that pooling is at least not slower
    assert pool_time <= direct_time * 1.1, "Buffer pool should not be significantly slower"
    
    print("  ✓ Performance improvement test passed")


def main():
    """Run all tests."""
    print("=" * 60)
    print("BUFFER POOL IMPLEMENTATION TESTS")
    print("=" * 60)
    
    try:
        test_basic_functionality()
        test_reuse_functionality()
        test_thread_safety()
        test_pool_size_limits()
        test_performance_improvement()
        
        print("\n" + "=" * 60)
        print("ALL TESTS PASSED! ✓")
        print("=" * 60)
        
    except Exception as e:
        print(f"\n❌ Test failed: {e}")
        import traceback
        traceback.print_exc()
        return False
    
    return True


if __name__ == "__main__":
    success = main()
    exit(0 if success else 1) 