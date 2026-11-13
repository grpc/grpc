"""Fork test for gRPC Python covering all four RPC types. Start server.py first.

    GRPC_ENABLE_FORK_SUPPORT=1 python client.py [CHILDREN] [ROUNDS] [TIMEOUT]
    GRPC_ENABLE_FORK_SUPPORT=0 python client.py                    # children hang
    GRPC_ENABLE_FORK_SUPPORT=0 python client.py --child-rpcs=none  # passes

The parent keeps calls of every kind in flight from background threads (RPCS
below) while its main thread keeps forking children (multiprocessing, "fork"
start method, the same os.fork() path a multiprocessing.Pool uses). Each child
then makes one call of each of the four types on the channel it inherited and
on a new channel (--child-rpcs). Every response is checked, in the parent and
in the children, and each child must exit 0 within TIMEOUT seconds:

  HANG   a child did not exit: its Python stack is dumped (SIGUSR1 ->
         faulthandler), then it is killed.
  CRASH  a child exited non-zero. 64 (os.EX_USAGE) means gRPC's post-fork
         handler raised (gRPC logs the exception class just before); 1 means
         the child's own call failed or got a wrong response (traceback above).

GRPC_ENABLE_FORK_SUPPORT (read when grpc is imported):
  0 (default)  gRPC does nothing around fork(). The child gets a frozen copy of
               the parent's calls and of gRPC's internal state, including locks
               held by threads that don't exist in the child. That is fine if
               the child never touches gRPC; any call it makes hangs.
  1            Before fork(), gRPC pauses its own threads. In the child, it
               resets its I/O and cancels every inherited call, so the child can
               make new calls on the inherited channel or on a new one.
"""

import argparse
import collections
import faulthandler
import logging
import multiprocessing
import os
import queue
import signal
import threading
import time

import grpc
from grpc._cython import cygrpc
import grpc.experimental

TARGET = "localhost:50065"
UNARY = "/forkdebug.Echo/Unary"
SERVER_STREAM = "/forkdebug.Echo/ServerStream"
CLIENT_STREAM = "/forkdebug.Echo/ClientStream"
BIDI = "/forkdebug.Echo/Bidi"
FOREVER = b"1000000000"  # Messages to ask for in a long-lived server stream.
THREE = [b"0", b"1", b"2"]  # What a server stream asked for 3 must deliver.
UPLOAD = (b"a", b"b", b"c")  # A short client stream; the server counts it.

# gRPC's own logger only has a NullHandler, which would hide post-fork errors.
logging.basicConfig(format="[pid %(process)d] %(levelname)s %(name)s: %(message)s")
faulthandler.enable()  # Native crashes print a stack.
faulthandler.register(signal.SIGUSR1, all_threads=True)  # Inherited by children.

stop = threading.Event()
errors = []
checked = collections.Counter()  # Responses checked in the parent, per variant.
live = {}  # In-flight calls, inherited by the children.

channel = grpc.insecure_channel(TARGET)
single_threaded_channel = grpc.insecure_channel(TARGET)
unary = channel.unary_unary(UNARY)
server_stream = channel.unary_stream(SERVER_STREAM)
single_threaded_server_stream = single_threaded_channel.unary_stream(SERVER_STREAM)
client_stream = channel.stream_unary(CLIENT_STREAM)
bidi = channel.stream_stream(BIDI)


def expect(expected, actual, name=None):
    """Fails unless actual == expected; counts parent checks under name."""
    if actual != expected:
        raise AssertionError(f"expected {expected!r}, got {actual!r}")
    if name:
        checked[name] += len(expected) if isinstance(expected, list) else 1


def unary_blocking(name, ready):
    """Blocking unary calls back to back."""
    n = 0
    while not stop.is_set():
        payload = b"%d" % n
        expect(payload, unary(payload, timeout=10), name)
        n += 1
        ready.set()


def unary_future(name, ready):
    """Unary .future() calls back to back."""
    n = 0
    while not stop.is_set():
        payload = b"%d" % n
        expect(payload, unary.future(payload, timeout=10).result(), name)
        n += 1
        ready.set()


def server_stream_long(name, ready):
    """One long-lived server stream; messages must arrive as 0, 1, 2, ..."""
    responses = server_stream(FOREVER)
    for expected, response in enumerate(responses):
        expect(str(expected).encode(), response, name)
        ready.set()
        if stop.is_set():
            responses.cancel()  # It would not end on its own.
            return


def server_stream_single_threaded(name, ready):
    """Short single-threaded server streams back to back."""
    while not stop.is_set():
        expect(THREE, list(single_threaded_server_stream(b"3", timeout=10)), name)
        ready.set()


def client_stream_blocking(name, ready):
    """Blocking client-streaming calls back to back."""
    while not stop.is_set():
        expect(b"3", client_stream(iter(UPLOAD), timeout=10), name)
        ready.set()


def client_stream_future(name, ready):
    """Client-streaming .future() calls back to back."""
    while not stop.is_set():
        future = client_stream.future(iter(UPLOAD), timeout=10)
        expect(b"3", future.result(), name)
        ready.set()


def bidi_long(name, ready):
    """One long-lived ping-pong stream: send a number, check its echo, repeat."""
    requests = queue.Queue()
    sent = 0
    requests.put(b"0")
    responses = bidi(iter(requests.get, None))
    live["bidi"] = responses
    for response in responses:
        expect(str(sent).encode(), response, name)
        ready.set()
        if stop.is_set():
            requests.put(None)  # Half-close: the stream then ends with OK.
        else:
            sent += 1
            requests.put(str(sent).encode())


def bidi_short(name, ready):
    """Short bidi streams back to back."""
    calls = 0
    while not stop.is_set():
        sent = [b"%d-%d" % (calls, i) for i in range(3)]
        expect(sent, list(bidi(iter(sent), timeout=10)), name)
        calls += 1
        ready.set()


# How gRPC Python's sync stack runs each kind of call:
#   segregated: completes on its own completion queue, polled by the calling
#               thread. After fork, the child cancels and abandons these.
#   integrated: completes on the channel's shared completion queue, polled by
#               a gRPC thread. After fork, the child cancels and drains these.
RPCS = {
    "unary": unary_blocking,  # segregated
    "unary_future": unary_future,  # integrated
    "server_stream": server_stream_long,  # integrated
    "server_stream_single_threaded": server_stream_single_threaded,  # segregated
    "client_stream": client_stream_blocking,  # segregated
    "client_stream_future": client_stream_future,  # integrated
    "bidi": bidi_long,  # integrated
    "bidi_short": bidi_short,  # integrated
}


def check_child_calls(child_channel):
    """One call of each of the four types, each response checked."""
    options = {"timeout": 5, "wait_for_ready": True}
    tag = b"child-%d" % os.getpid()
    expect(tag, child_channel.unary_unary(UNARY)(tag, **options))
    expect(THREE, list(child_channel.unary_stream(SERVER_STREAM)(b"3", **options)))
    expect(b"3", child_channel.stream_unary(CLIENT_STREAM)(iter(UPLOAD), **options))
    sent = [b"%s-%d" % (tag, i) for i in range(3)]
    expect(sent, list(child_channel.stream_stream(BIDI)(iter(sent), **options)))


def child_main(child_rpcs, report):
    """Runs in each forked child, after gRPC's post-fork handler (if any)."""
    if report and "bidi" in live:
        # Read without taking the call's lock: .code() would wait for the call
        # to end, which never happens if nothing cleaned it up.
        status = live["bidi"]._state.code  # pylint: disable=protected-access
        what = "cancelled by gRPC after fork" if status else "frozen as it was"
        print(
            f"child {os.getpid()}: the parent's in-flight bidi stream, as"
            f" inherited: {status} ({what})",
            flush=True,
        )
    for which in child_rpcs:
        if which == "inherited":
            check_child_calls(channel)
        else:
            with grpc.insecure_channel(TARGET) as new_channel:
                check_child_calls(new_channel)


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
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("children", nargs="?", type=int, default=4)
    parser.add_argument("rounds", nargs="?", type=int, default=20)
    parser.add_argument("timeout", nargs="?", type=float, default=10)
    parser.add_argument(
        "--rpcs",
        default=",".join(RPCS),
        help="parent calls, comma-separated (default: %(default)s)",
    )
    parser.add_argument(
        "--child-rpcs",
        default="inherited,new",
        help="channels each child makes its calls on: inherited,new or none"
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
        f"parent pid {os.getpid()}, grpcio {grpc.__version__} from"
        f" {os.path.dirname(grpc.__file__)}; fork support"
        f" {'ON' if cygrpc.is_fork_support_enabled() else 'OFF'}"
        f" (GRPC_ENABLE_FORK_SUPPORT={os.environ.get('GRPC_ENABLE_FORK_SUPPORT')});"
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
    before_forks = collections.Counter(checked)

    ctx = multiprocessing.get_context("fork")
    for round_num in range(1, args.rounds + 1):
        children = [
            ctx.Process(target=child_main, args=(child_rpcs, round_num == i == 1))
            for i in range(1, args.children + 1)
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
            fail(f"FAIL: parent call failed: {errors[0]}")
        print(f"round {round_num}/{args.rounds}: {args.children} forks ok", flush=True)

    # Wind the parent's calls down so long-lived streams get checked too.
    stop.set()
    for name, thread in threads.items():
        thread.join(30)
        if thread.is_alive():
            fail(f"FAIL: {name} did not finish after the test stopped")
    if errors:
        fail(f"FAIL: parent call failed: {errors[0]}")
    during_forks = {name: checked[name] - before_forks[name] for name in rpcs}
    stalled = [name for name, count in during_forks.items() if count <= 0]
    if stalled:
        fail(f"FAIL: no correct responses while forking for {stalled}")
    channel.close()
    single_threaded_channel.close()
    forks = args.rounds * args.children
    print(
        f"DONE: {forks} forks. Children made and checked"
        f" {forks * 4 * len(child_rpcs)} calls ({args.child_rpcs} channels)."
        f" Parent responses checked while forking: {during_forks}.",
        flush=True,
    )


if __name__ == "__main__":
    main()
