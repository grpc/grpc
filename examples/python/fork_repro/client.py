"""
    GRPC_ENABLE_FORK_SUPPORT=0 python client.py
    GRPC_ENABLE_FORK_SUPPORT=1 python client.py # hangs
"""

import multiprocessing
import os
import sys
import threading

import grpc

WORKERS = int(sys.argv[1]) if len(sys.argv) > 1 else 4

channel = grpc.insecure_channel("localhost:50061")
echo = channel.unary_unary("/forkdebug.Echo/Unary")


def busy():
    while True:
        echo(b"ping", timeout=10)


def work(i):
    return i * i  # No gRPC in the workers.


if __name__ == "__main__":
    print(
        f"parent pid {os.getpid()}, GRPC_ENABLE_FORK_SUPPORT="
        f"{os.environ.get('GRPC_ENABLE_FORK_SUPPORT')}",
        flush=True,
    )
    echo(b"hello", timeout=10)
    threading.Thread(target=busy, daemon=True).start()
    with multiprocessing.get_context("fork").Pool(WORKERS) as pool:
        pids = [p.pid for p in multiprocessing.active_children()]
        print(f"worker pids {pids}", flush=True)
        print(pool.map(work, range(8)), flush=True)
    print("DONE", flush=True)
