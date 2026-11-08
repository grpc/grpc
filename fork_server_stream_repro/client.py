"""Fork test for gRPC Python with in-flight server-streaming RPCs.

Start server.py first, then:

    GRPC_ENABLE_FORK_SUPPORT=0 python client.py
    GRPC_ENABLE_FORK_SUPPORT=1 python client.py [CHILDREN] [ROUNDS] [TIMEOUT]
    GRPC_ENABLE_FORK_SUPPORT=1 python client.py --rpcs=short --child-rpcs=new

Background threads keep server-streaming RPCs in flight (every variant in RPCS
below, unless --rpcs says otherwise) while the main thread keeps forking child
processes (multiprocessing, "fork" start method, the same os.fork() path a
multiprocessing.Pool uses). Every child runs gRPC's __postfork_child ->
Channel.cancel_calls_on_fork(), which has to clean up the inherited calls
before the child gets to run any user code.

Responses are checked on both sides of every fork:
  parent    every stream must deliver exactly b"0", b"1", b"2", ... in order,
            so lost, duplicated, reordered or misrouted messages fail the test.
            Long-lived streams are wound down and checked at the end.
  children  each child makes its own server-streaming call on the channels it
            inherited and/or on a new channel (--child-rpcs) and checks it.
            That needs fork support, so it is off without
            GRPC_ENABLE_FORK_SUPPORT.

Each child must exit 0 within TIMEOUT seconds:
  HANG   child did not exit: its Python stack is dumped (SIGUSR1 ->
         faulthandler), then it is killed.
  CRASH  child exited non-zero. 64 (os.EX_USAGE) means gRPC's post-fork
         handler raised (gRPC logs the exception class just before); 1 means
         the child's own call failed or got a wrong response (traceback above).
"""

import argparse
import collections
import faulthandler
import functools
import logging
import multiprocessing
import os
import signal
import threading
import time

import grpc
import grpc.experimental

TARGET = "localhost:50063"
METHOD = "/forkdebug.Echo/ServerStream"
FOREVER = b"1000000000"  # Messages to ask for in a long-lived stream.
SHORT = [b"0", b"1", b"2"]  # What a short stream (asking for 3) must deliver.
# With this option, server-streaming calls are polled by the calling thread
# instead of the channel's polling thread.
SINGLE_THREADED = ((grpc.experimental.ChannelOptions.SingleThreadedUnaryStream, 1),)

# gRPC's own logger only has a NullHandler, which would hide post-fork errors.
logging.basicConfig(format="[pid %(process)d] %(levelname)s %(name)s: %(message)s")
faulthandler.enable()  # Native crashes print a stack.
faulthandler.register(signal.SIGUSR1, all_threads=True)  # Inherited by children.

stop = threading.Event()
errors = []
checked = collections.Counter()  # Responses checked in the parent, per variant.

channel = grpc.insecure_channel(TARGET)
single_threaded_channel = grpc.insecure_channel(TARGET, options=SINGLE_THREADED)
server_stream = channel.unary_stream(METHOD)
single_threaded_server_stream = single_threaded_channel.unary_stream(METHOD)


def long_stream(rpc, name, ready):
    """One long-lived stream, checked message by message."""
    responses = rpc(FOREVER)
    for expected, response in enumerate(responses):
        if response != str(expected).encode():
            raise AssertionError(f"expected message {expected}, got {response!r}")
        checked[name] += 1
        ready.set()
        if stop.is_set():
            responses.cancel()  # It would not end on its own.
            return


def short_streams(rpc, name, ready):
    """Short streams back to back (call creation and completion)."""
    while not stop.is_set():
        received = list(rpc(b"3", timeout=10))
        if received != SHORT:
            raise AssertionError(f"expected {SHORT}, got {received}")
        checked[name] += len(received)
        ready.set()


# How gRPC Python's sync stack runs each variant:
#   integrated: completes on the channel's shared completion queue, polled by
#               a gRPC thread; a fork child cancels and drains these calls.
#   segregated: completes on its own completion queue, polled by the calling
#               thread; a fork child cancels and abandons these calls.
RPCS = {
    "long": functools.partial(long_stream, server_stream),  # integrated
    "short": functools.partial(short_streams, server_stream),  # integrated
    "single_threaded_long": functools.partial(  # segregated
        long_stream, single_threaded_server_stream
    ),
    "single_threaded_short": functools.partial(  # segregated
        short_streams, single_threaded_server_stream
    ),
}


def check_child_call(child_channel):
    """One server-streaming call from a forked child; must deliver SHORT."""
    stream = child_channel.unary_stream(METHOD)
    received = list(stream(b"3", timeout=5, wait_for_ready=True))
    if received != SHORT:
        raise AssertionError(f"child expected {SHORT}, got {received}")


def child_main(child_rpcs):
    """Runs in each forked child, so gRPC's post-fork handler has returned."""
    for which in child_rpcs:
        if which == "inherited":
            check_child_call(channel)
            check_child_call(single_threaded_channel)
        else:
            with grpc.insecure_channel(TARGET) as new_channel:
                check_child_call(new_channel)
            with grpc.insecure_channel(TARGET, options=SINGLE_THREADED) as new_channel:
                check_child_call(new_channel)


def run(name, ready):
    try:
        RPCS[name](name, ready)
    except Exception as e:  # pylint: disable=broad-except
        errors.append(f"{name}: {e!r}")


def fail(message, stuck=()):
    print(message, flush=True)
    for pid in stuck:
        print(f"--- child {pid}:", flush=True)
        os.kill(pid, signal.SIGUSR1)  # faulthandler prints the stuck stack.
        time.sleep(0.5)
    for process in multiprocessing.active_children():
        process.kill()
    os._exit(1)


def main():
    fork_support = os.environ.get("GRPC_ENABLE_FORK_SUPPORT", "").lower() in (
        "1",
        "true",
        "yes",
    )
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("children", nargs="?", type=int, default=4)
    parser.add_argument("rounds", nargs="?", type=int, default=20)
    parser.add_argument("timeout", nargs="?", type=float, default=10)
    parser.add_argument(
        "--rpcs", default=",".join(RPCS), help="comma-separated, default: %(default)s"
    )
    parser.add_argument(
        "--child-rpcs",
        default="inherited,new" if fork_support else "none",
        help="channels each child checks calls on: inherited,new or none"
        " (default: %(default)s)",
    )
    args = parser.parse_args()
    rpcs = args.rpcs.split(",")
    if not set(rpcs) <= set(RPCS):
        parser.error(f"--rpcs must be a subset of {','.join(RPCS)}")
    child_rpcs = [] if args.child_rpcs == "none" else args.child_rpcs.split(",")
    if not set(child_rpcs) <= {"inherited", "new"}:
        parser.error("--child-rpcs must be a subset of inherited,new or none")
    print(
        f"parent pid {os.getpid()}, grpcio {grpc.__version__} from "
        f"{os.path.dirname(grpc.__file__)}, GRPC_ENABLE_FORK_SUPPORT="
        f"{os.environ.get('GRPC_ENABLE_FORK_SUPPORT')}, rpcs={','.join(rpcs)},"
        f" child-rpcs={args.child_rpcs}",
        flush=True,
    )
    ready = {name: threading.Event() for name in rpcs}
    threads = {
        name: threading.Thread(target=run, args=(name, ready[name]), daemon=True)
        for name in rpcs
    }
    for thread in threads.values():
        thread.start()
    for name, event in ready.items():
        if not event.wait(10) or errors:
            fail(f"FAIL: {name} did not get going: {errors or 'timed out'}")

    ctx = multiprocessing.get_context("fork")
    for round_num in range(1, args.rounds + 1):
        children = [
            ctx.Process(target=child_main, args=(child_rpcs,))
            for _ in range(args.children)
        ]
        for child in children:
            child.start()
        deadline = time.monotonic() + args.timeout
        for child in children:
            child.join(max(0.0, deadline - time.monotonic()))
        stuck = [child.pid for child in children if child.is_alive()]
        if stuck:
            fail(
                f"HANG in round {round_num}: children {stuck} did not exit"
                f" within {args.timeout}s. Their stacks:",
                stuck,
            )
        crashed = {child.pid: child.exitcode for child in children if child.exitcode}
        if crashed:
            fail(f"CRASH in round {round_num}: {{pid: exit code}} = {crashed}")
        if errors:
            fail(f"FAIL: background RPC failed: {errors[0]}")
        print(f"round {round_num}/{args.rounds}: {args.children} forks ok", flush=True)

    # Wind the background streams down so long-lived ones get checked too.
    stop.set()
    for name, thread in threads.items():
        thread.join(30)
        if thread.is_alive():
            fail(f"FAIL: {name} did not finish after the test stopped")
    if errors:
        fail(f"FAIL: background RPC failed: {errors[0]}")
    channel.close()
    single_threaded_channel.close()
    forks = args.rounds * args.children
    child_summary = (
        f"each child checked its own calls on {args.child_rpcs} channel(s),"
        " default and single-threaded"
        if child_rpcs
        else "children made no calls"
    )
    print(
        f"DONE: {forks} forks with in-flight server-streaming RPCs;"
        f" {child_summary}. Parent responses checked: {dict(checked)}.",
        flush=True,
    )


if __name__ == "__main__":
    main()
