"""Fork test for gRPC Python with in-flight bidi streams. Start server.py first.

    GRPC_ENABLE_FORK_SUPPORT=0 python client.py
    GRPC_ENABLE_FORK_SUPPORT=1 python client.py [CHILDREN] [ROUNDS] [TIMEOUT]
    GRPC_ENABLE_FORK_SUPPORT=1 python client.py --rpcs=short --child-rpcs=new

Background threads keep bidi streams in flight on one channel (every variant in
RPCS below, unless --rpcs says otherwise) while the main thread keeps forking
child processes (multiprocessing, "fork" start method, the same os.fork() path
a multiprocessing.Pool uses). Every child runs gRPC's __postfork_child ->
Channel.cancel_calls_on_fork(), which has to clean up the inherited calls
before the child gets to run any user code.

Responses are checked on both sides of every fork:
  parent    every echo of the background streams must match what was sent, so
            lost, duplicated, reordered or misrouted messages fail the test.
            Long-lived streams are wound down and checked at the end.
  children  each child makes its own bidi call on the channel it inherited
            and/or on a new channel (--child-rpcs) and checks the echoes. That
            needs fork support, so it is off without GRPC_ENABLE_FORK_SUPPORT.

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
import logging
import multiprocessing
import os
import queue
import signal
import threading
import time

import grpc

TARGET = "localhost:50062"
METHOD = "/forkdebug.Echo/Bidi"

# gRPC's own logger only has a NullHandler, which would hide post-fork errors.
logging.basicConfig(format="[pid %(process)d] %(levelname)s %(name)s: %(message)s")
faulthandler.enable()  # Native crashes print a stack.
faulthandler.register(signal.SIGUSR1, all_threads=True)  # Inherited by children.

stop = threading.Event()
errors = []
checked = collections.Counter()  # Responses checked in the parent, per variant.

channel = grpc.insecure_channel(TARGET)
bidi = channel.stream_stream(METHOD)


def long_stream(name, ready):
    """One long-lived ping-pong stream: send a number, check its echo, repeat."""
    requests = queue.Queue()
    sent = 0
    requests.put(b"0")
    for response in bidi(iter(requests.get, None)):
        if response != str(sent).encode():
            raise AssertionError(f"sent {sent}, got back {response!r}")
        checked[name] += 1
        ready.set()
        if stop.is_set():
            requests.put(None)  # Half-close: the stream then ends with OK.
        else:
            sent += 1
            requests.put(str(sent).encode())


def short_streams(name, ready):
    """Short streams back to back (call creation and completion)."""
    calls = 0
    while not stop.is_set():
        sent = [b"%d-%d" % (calls, i) for i in range(3)]
        received = list(bidi(iter(sent), timeout=10))
        if received != sent:
            raise AssertionError(f"sent {sent}, got back {received}")
        checked[name] += len(sent)
        calls += 1
        ready.set()


RPCS = {
    "long": long_stream,
    "short": short_streams,
}


def check_child_call(child_channel):
    """One bidi call from a forked child; the echoes must match."""
    sent = [b"child-%d-%d" % (os.getpid(), i) for i in range(3)]
    stream = child_channel.stream_stream(METHOD)
    received = list(stream(iter(sent), timeout=5, wait_for_ready=True))
    if received != sent:
        raise AssertionError(f"child sent {sent}, got back {received}")


def child_main(child_rpcs):
    """Runs in each forked child, so gRPC's post-fork handler has returned."""
    for which in child_rpcs:
        if which == "inherited":
            check_child_call(channel)
        else:
            with grpc.insecure_channel(TARGET) as new_channel:
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
        help="channels each child checks a call on: inherited,new or none"
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
    forks = args.rounds * args.children
    child_summary = (
        f"each child checked its own call on {args.child_rpcs} channel(s)"
        if child_rpcs
        else "children made no calls"
    )
    print(
        f"DONE: {forks} forks with in-flight bidi streams; {child_summary}."
        f" Parent responses checked: {dict(checked)}.",
        flush=True,
    )


if __name__ == "__main__":
    main()
