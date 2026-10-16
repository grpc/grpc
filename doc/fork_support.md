# Background

In Python, multithreading is ineffective at concurrency for CPU bound tasks due
to the GIL (global interpreter lock).  Extension modules can release the GIL in
CPU bound tasks, but that isn't an option in pure Python. Users use libraries
such as multiprocessing, subprocess, concurrent.futures.ProcessPoolExecutor,
etc, to work around the GIL. These modules call `fork()` underneath the hood.
Various issues have been reported when using these modules with gRPC Python.
gRPC Python wraps gRPC core, which uses multithreading for performance, and
hence doesn't support `fork()`. Historically, we didn't support forking in gRPC,
but some users seemed to be doing fine until their code started to break on
version 1.6.  This was likely caused by the addition of background c-threads and
a background Python thread.

# Current Status

gRPC Python applications can enable client-side forking when two environment
variables are given:

```
export GRPC_ENABLE_FORK_SUPPORT=true
export GRPC_POLL_STRATEGY=poll
```

The fork-support effort only works with "epoll1" and "poll" polling strategy.
There is no active development to extend fork-support's coverage.

For more details about poll strategy setting, see
https://github.com/grpc/grpc/blob/master/doc/environment_variables.md.

# Alternative: use after fork

Complexities mentioned in the background section are inevitable for "pre-fork"
usage, where the application creates gRPC Python objects (e.g., client channel)
before invoking `fork()`. However, if the application only instantiate gRPC
Python objects after calling `fork()`, then `fork()` will work normally, since
there is no C extension binding at this point. This idea is demonstrated by the
[multiprocessing
example](https://github.com/grpc/grpc/tree/master/examples/python/multiprocessing).
