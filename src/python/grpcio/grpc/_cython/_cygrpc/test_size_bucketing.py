#!/usr/bin/env python3
"""
Test script for the size-bucketed buffer pool implementation.
This demonstrates how different message sizes are grouped into buckets.
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from grpc._cython._cygrpc import get_global_buffer_pool

def test_size_bucketing():
    """Test that messages are properly bucketed by size."""
    pool = get_global_buffer_pool()
    
    # Test different message sizes and their bucket assignments
    test_sizes = [
        (1, "1 byte"),
        (5, "5 bytes"),
        (10, "10 bytes"),
        (50, "50 bytes"),
        (100, "100 bytes"),
        (500, "500 bytes"),
        (1000, "1KB"),
        (5000, "5KB"),
        (10000, "10KB"),
        (50000, "50KB"),
        (100000, "100KB"),
        (500000, "500KB"),
        (1000000, "1MB"),
        (5000000, "5MB"),
        (10000000, "10MB"),
        (50000000, "50MB"),
        (100000000, "100MB"),
        (200000000, "200MB (larger than max bucket)")
    ]
    
    print("Testing size bucketing...")
    print("Initial bucket sizes:", pool._bucket_sizes)
    print()
    
    for size, description in test_sizes:
        message = b"x" * size
        bucket_size = pool._get_bucket_size(size)
        print(f"{description:15} ({size:9,} bytes) → Bucket: {bucket_size:,} bytes")
    
    print(f"\nFinal bucket sizes: {pool._bucket_sizes}")

def test_dynamic_bucket_creation():
    """Test that new buckets are created dynamically for large messages."""
    pool = get_global_buffer_pool()
    
    print("\nTesting dynamic bucket creation...")
    print(f"Initial bucket count: {len(pool._bucket_sizes)}")
    print(f"Initial buckets: {pool._bucket_sizes}")
    
    # Test with progressively larger messages
    test_sizes = [
        100000000,   # 100MB - should use existing bucket
        200000000,   # 200MB - should create new bucket (1GB)
        500000000,   # 500MB - should use the 1GB bucket
        1500000000,  # 1.5GB - should create new bucket (10GB)
        5000000000,  # 5GB - should use the 10GB bucket
    ]
    
    for size in test_sizes:
        message = b"x" * size
        bucket_size = pool._get_bucket_size(size)
        print(f"{size:12,} bytes → Bucket: {bucket_size:,} bytes")
    
    print(f"\nFinal bucket count: {len(pool._bucket_sizes)}")
    print(f"Final buckets: {pool._bucket_sizes}")
    
    # Verify that new buckets were created
    if len(pool._bucket_sizes) > 9:  # Started with 9 buckets
        print("✅ SUCCESS: New buckets were created dynamically!")
    else:
        print("❌ FAILURE: No new buckets were created")

def test_bucket_efficiency():
    """Test that buffers are reused within the same bucket."""
    pool = get_global_buffer_pool()
    
    print("\nTesting bucket efficiency...")
    
    # Test with messages that should use the same bucket
    small_messages = [
        b"hello",           # 5 bytes → 10B bucket
        b"world",           # 5 bytes → 10B bucket
        b"test",            # 4 bytes → 10B bucket
    ]
    
    medium_messages = [
        b"x" * 500,         # 500 bytes → 1KB bucket
        b"y" * 750,         # 750 bytes → 1KB bucket
        b"z" * 999,         # 999 bytes → 1KB bucket
    ]
    
    print("Testing small messages (should reuse 10B bucket):")
    buffers = []
    for msg in small_messages:
        buffer = pool.get_buffer(msg)
        buffers.append(buffer)
    
    # Return all buffers
    for i, msg in enumerate(small_messages):
        pool.return_buffer(buffers[i], len(msg))
    
    # Get buffers again - should reuse
    for msg in small_messages:
        buffer = pool.get_buffer(msg)
        pool.return_buffer(buffer, len(msg))
    
    print("Testing medium messages (should reuse 1KB bucket):")
    buffers = []
    for msg in medium_messages:
        buffer = pool.get_buffer(msg)
        buffers.append(buffer)
    
    # Return all buffers
    for i, msg in enumerate(medium_messages):
        pool.return_buffer(buffers[i], len(msg))
    
    # Get buffers again - should reuse
    for msg in medium_messages:
        buffer = pool.get_buffer(msg)
        pool.return_buffer(buffer, len(msg))
    
    stats = pool.get_stats()
    print(f"\nFinal stats:")
    print(f"Total allocations: {stats['total_allocations']}")
    print(f"Total reuses: {stats['total_reuses']}")
    print(f"Reuse rate: {stats['reuse_rate']:.1f}%")
    print(f"Bucket stats: {stats['bucket_stats']}")

def test_memory_efficiency():
    """Test that bucketing reduces memory usage compared to per-size pools."""
    pool = get_global_buffer_pool()
    
    print("\nTesting memory efficiency...")
    
    # Simulate many different message sizes
    import random
    random.seed(42)  # For reproducible results
    
    # Generate 1000 messages with sizes from 1 to 100KB
    messages = []
    for _ in range(1000):
        size = random.randint(1, 100000)
        message = b"x" * size
        messages.append(message)
    
    print(f"Generated {len(messages)} messages with sizes 1-100KB")
    
    # Allocate buffers
    buffers = []
    for msg in messages:
        buffer = pool.get_buffer(msg)
        buffers.append(buffer)
    
    # Return all buffers
    for i, msg in enumerate(messages):
        pool.return_buffer(buffers[i], len(msg))
    
    # Get buffers again - should reuse from buckets
    for msg in messages:
        buffer = pool.get_buffer(msg)
        pool.return_buffer(buffer, len(msg))
    
    stats = pool.get_stats()
    
    print(f"\nResults:")
    print(f"Unique message sizes: {len(set(len(msg) for msg in messages))}")
    print(f"Number of buckets used: {len([s for s in stats['bucket_stats'].values() if s > 0])}")
    print(f"Total allocations: {stats['total_allocations']}")
    print(f"Total reuses: {stats['total_reuses']}")
    print(f"Reuse rate: {stats['reuse_rate']:.1f}%")
    
    # Show bucket usage
    print(f"\nBucket usage:")
    for bucket_size, count in stats['bucket_stats'].items():
        if count > 0:
            print(f"  {bucket_size:,} bytes: {count} buffers")

def test_performance_comparison():
    """Compare performance with and without bucketing."""
    pool = get_global_buffer_pool()
    
    print("\nTesting performance comparison...")
    
    # Test with messages that would create many different sizes
    import time
    
    # Generate messages with many different sizes
    messages = []
    for i in range(1000):
        size = i + 1  # Sizes 1, 2, 3, ..., 1000
        message = b"x" * size
        messages.append(message)
    
    print(f"Testing with {len(messages)} unique message sizes...")
    
    # First round - all allocations
    start_time = time.time()
    buffers = []
    for msg in messages:
        buffer = pool.get_buffer(msg)
        buffers.append(buffer)
    
    allocation_time = time.time() - start_time
    
    # Return all buffers
    start_time = time.time()
    for i, msg in enumerate(messages):
        pool.return_buffer(buffers[i], len(msg))
    
    return_time = time.time() - start_time
    
    # Second round - should reuse from buckets
    start_time = time.time()
    buffers2 = []
    for msg in messages:
        buffer = pool.get_buffer(msg)
        buffers2.append(buffer)
    
    reuse_time = time.time() - start_time
    
    # Return second batch
    for i, msg in enumerate(messages):
        pool.return_buffer(buffers2[i], len(msg))
    
    stats = pool.get_stats()
    
    print(f"First allocation: {allocation_time:.4f}s")
    print(f"Return time: {return_time:.4f}s")
    print(f"Reuse allocation: {reuse_time:.4f}s")
    print(f"Speedup: {allocation_time/reuse_time:.2f}x")
    print(f"Reuse rate: {stats['reuse_rate']:.1f}%")
    
    # Calculate memory savings
    unique_sizes = len(set(len(msg) for msg in messages))
    buckets_used = len([s for s in stats['bucket_stats'].values() if s > 0])
    memory_savings = (unique_sizes - buckets_used) / unique_sizes * 100
    
    print(f"Memory savings: {memory_savings:.1f}% (from {unique_sizes} sizes to {buckets_used} buckets)")

if __name__ == "__main__":
    test_size_bucketing()
    test_dynamic_bucket_creation()
    test_bucket_efficiency()
    test_memory_efficiency()
    test_performance_comparison()
    
    # Clean up
    pool = get_global_buffer_pool()
    pool.clear_pools()
    
    print("\nSize bucketing test completed!") 