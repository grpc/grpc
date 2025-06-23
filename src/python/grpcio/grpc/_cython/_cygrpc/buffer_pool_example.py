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
Example demonstrating the buffer pool optimization in gRPC Python.
"""

import time
import threading
from typing import List

# Import the buffer pool for monitoring
from .buffer_pool import get_global_buffer_pool


def create_test_messages() -> List[bytes]:
    """Create test messages of different sizes."""
    return [
        b"Hello, World!",                    # 13 bytes
        b"x" * 1024,                        # 1KB
        b"y" * 4096,                        # 4KB
        b"z" * 16384,                       # 16KB
        b"w" * 65536,                       # 64KB
    ]


def simulate_grpc_operations(messages: List[bytes], iterations: int = 1000):
    """Simulate gRPC operations using the optimized buffer pool."""
    print(f"Simulating {iterations} gRPC operations with {len(messages)} different message sizes...")
    
    # Get the global buffer pool for monitoring
    pool = get_global_buffer_pool()
    
    # Clear any existing statistics
    pool.clear_pools()
    
    start_time = time.perf_counter()
    
    # Simulate gRPC operations
    for i in range(iterations):
        for message in messages:
            # This simulates what happens in SendMessageOperation
            import grpc
            from grpc._cython import cygrpc
            
            # Create operation (this will use the buffer pool internally)
            operation = cygrpc.SendMessageOperation(message, 0)
            operation.c()
            operation.un_c()
    
    end_time = time.perf_counter()
    total_time = end_time - start_time
    
    # Get final statistics
    stats = pool.get_stats()
    
    print(f"Completed {iterations * len(messages)} operations in {total_time:.4f} seconds")
    print(f"Operations per second: {(iterations * len(messages)) / total_time:.0f}")
    print()
    
    return stats, total_time


def demonstrate_buffer_reuse():
    """Demonstrate how buffers are reused."""
    print("=" * 60)
    print("DEMONSTRATING BUFFER REUSE")
    print("=" * 60)
    
    pool = get_global_buffer_pool()
    pool.clear_pools()
    
    message = b"x" * 1024
    message_size = len(message)
    
    print(f"Testing with message size: {message_size} bytes")
    print()
    
    # First batch of operations
    print("First batch (should allocate new buffers):")
    for i in range(5):
        buffer = pool.get_buffer(message)
        pool.return_buffer(buffer, message_size)
    
    stats1 = pool.get_stats()
    print(f"  Allocations: {stats1['total_allocations']}")
    print(f"  Reuses: {stats1['total_reuses']}")
    print(f"  Reuse rate: {stats1['reuse_rate']:.1f}%")
    print()
    
    # Second batch of operations (should reuse buffers)
    print("Second batch (should reuse buffers):")
    for i in range(5):
        buffer = pool.get_buffer(message)
        pool.return_buffer(buffer, message_size)
    
    stats2 = pool.get_stats()
    print(f"  Allocations: {stats2['total_allocations']}")
    print(f"  Reuses: {stats2['total_reuses']}")
    print(f"  Reuse rate: {stats2['reuse_rate']:.1f}%")
    print()


def main():
    """Main example function."""
    print("=" * 80)
    print("gRPC PYTHON BUFFER POOL OPTIMIZATION EXAMPLE")
    print("=" * 80)
    print()
    
    # Demonstrate buffer reuse
    demonstrate_buffer_reuse()
    print()
    
    # Demonstrate performance
    messages = create_test_messages()
    stats, total_time = simulate_grpc_operations(messages, 1000)
    
    print("Performance results:")
    print(f"  Reuse rate: {stats['reuse_rate']:.1f}%")
    print(f"  Total allocations: {stats['total_allocations']}")
    print(f"  Total reuses: {stats['total_reuses']}")
    print()
    
    print("Example completed!")


if __name__ == "__main__":
    main() 