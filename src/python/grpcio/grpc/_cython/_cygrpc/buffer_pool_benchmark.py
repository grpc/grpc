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
Benchmark script to test buffer pool optimization performance.
"""

import time
import threading
import statistics
from typing import List, Dict, Any

# Import the buffer pool
from .buffer_pool import get_global_buffer_pool, BufferPool


def benchmark_buffer_pool_vs_direct_allocation(
    message_sizes: List[int] = [1024, 4096, 16384, 65536],
    iterations: int = 10000,
    num_threads: int = 4
) -> Dict[str, Any]:
    """
    Benchmark buffer pool vs direct allocation.
    
    Args:
        message_sizes: List of message sizes to test
        iterations: Number of iterations per test
        num_threads: Number of threads to use for concurrent testing
        
    Returns:
        Dictionary with benchmark results
    """
    
    def create_test_message(size: int) -> bytes:
        """Create a test message of given size."""
        return b'x' * size
    
    def benchmark_direct_allocation(message: bytes) -> float:
        """Benchmark direct allocation without pooling."""
        start_time = time.perf_counter()
        
        # Simulate the old behavior (without pooling)
        # This is what the original code did
        import grpc
        from grpc._cython import cygrpc
        
        # Create a dummy operation to test allocation overhead
        operation = cygrpc.SendMessageOperation(message, 0)
        operation.c()
        operation.un_c()
        
        end_time = time.perf_counter()
        return (end_time - start_time) * 1000000  # Convert to microseconds
    
    def benchmark_buffer_pool(message: bytes) -> float:
        """Benchmark buffer pool allocation."""
        start_time = time.perf_counter()
        
        # Use the new buffer pool
        pool = get_global_buffer_pool()
        buffer = pool.get_buffer(message)
        pool.return_buffer(buffer, len(message))
        
        end_time = time.perf_counter()
        return (end_time - start_time) * 1000000  # Convert to microseconds
    
    def run_benchmark_thread(
        message_size: int,
        iterations: int,
        results: List[float],
        benchmark_func
    ):
        """Run benchmark in a thread."""
        message = create_test_message(message_size)
        thread_results = []
        
        for _ in range(iterations):
            duration = benchmark_func(message)
            thread_results.append(duration)
        
        results.extend(thread_results)
    
    results = {}
    
    for message_size in message_sizes:
        print(f"Benchmarking message size: {message_size} bytes")
        
        # Test direct allocation
        direct_results = []
        threads = []
        for _ in range(num_threads):
            thread_results = []
            thread = threading.Thread(
                target=run_benchmark_thread,
                args=(message_size, iterations // num_threads, thread_results, benchmark_direct_allocation)
            )
            threads.append(thread)
            direct_results.append(thread_results)
        
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        
        # Flatten results
        direct_times = [t for sublist in direct_results for t in sublist]
        
        # Test buffer pool
        pool_results = []
        threads = []
        for _ in range(num_threads):
            thread_results = []
            thread = threading.Thread(
                target=run_benchmark_thread,
                args=(message_size, iterations // num_threads, thread_results, benchmark_buffer_pool)
            )
            threads.append(thread)
            pool_results.append(thread_results)
        
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        
        # Flatten results
        pool_times = [t for sublist in pool_results for t in sublist]
        
        # Calculate statistics
        direct_mean = statistics.mean(direct_times)
        direct_median = statistics.median(direct_times)
        direct_std = statistics.stdev(direct_times) if len(direct_times) > 1 else 0
        
        pool_mean = statistics.mean(pool_times)
        pool_median = statistics.median(pool_times)
        pool_std = statistics.stdev(pool_times) if len(pool_times) > 1 else 0
        
        improvement = ((direct_mean - pool_mean) / direct_mean) * 100
        
        results[message_size] = {
            'direct_allocation': {
                'mean_us': direct_mean,
                'median_us': direct_median,
                'std_us': direct_std,
                'min_us': min(direct_times),
                'max_us': max(direct_times)
            },
            'buffer_pool': {
                'mean_us': pool_mean,
                'median_us': pool_median,
                'std_us': pool_std,
                'min_us': min(pool_times),
                'max_us': max(pool_times)
            },
            'improvement_percent': improvement
        }
        
        print(f"  Direct allocation: {direct_mean:.2f} ± {direct_std:.2f} μs")
        print(f"  Buffer pool: {pool_mean:.2f} ± {pool_std:.2f} μs")
        print(f"  Improvement: {improvement:.1f}%")
        print()
    
    return results


def print_benchmark_summary(results: Dict[str, Any]):
    """Print a summary of benchmark results."""
    print("=" * 80)
    print("BUFFER POOL OPTIMIZATION BENCHMARK SUMMARY")
    print("=" * 80)
    
    total_improvement = 0
    num_tests = 0
    
    for message_size, result in results.items():
        improvement = result['improvement_percent']
        total_improvement += improvement
        num_tests += 1
        
        print(f"Message size {message_size:>6} bytes:")
        print(f"  Direct: {result['direct_allocation']['mean_us']:>8.2f} μs")
        print(f"  Pool:   {result['buffer_pool']['mean_us']:>8.2f} μs")
        print(f"  Speedup: {improvement:>6.1f}%")
        print()
    
    if num_tests > 0:
        avg_improvement = total_improvement / num_tests
        print(f"Average improvement: {avg_improvement:.1f}%")
    
    # Print pool statistics
    pool = get_global_buffer_pool()
    stats = pool.get_stats()
    print(f"\nBuffer pool statistics:")
    print(f"  Total allocations: {stats['total_allocations']}")
    print(f"  Total reuses: {stats['total_reuses']}")
    print(f"  Reuse rate: {stats['reuse_rate']:.1f}%")
    print(f"  Pool sizes: {stats['pool_sizes']}")


def main():
    """Main benchmark function."""
    print("Starting buffer pool optimization benchmark...")
    print("This will test the performance improvement of buffer pooling")
    print("over direct allocation for gRPC message buffers.\n")
    
    # Run benchmark
    results = benchmark_buffer_pool_vs_direct_allocation(
        message_sizes=[1024, 4096, 16384, 65536],
        iterations=10000,
        num_threads=4
    )
    
    # Print summary
    print_benchmark_summary(results)
    
    print("\nBenchmark completed!")


if __name__ == "__main__":
    main() 