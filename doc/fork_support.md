# Background #

In Python, multithreading is ineffective at concurrency for CPU bound tasks
due to the GIL (global interpreter lock).  Extension modules can release
the GIL in CPU bound tasks, but that isn't an option in pure Python.
Users use libraries such as multiprocessing, subprocess, concurrent.futures.ProcessPoolExecutor,
etc, to work around the GIL. These modules call ```fork()``` underneath the hood. Various issues have
been reported when using these modules with gRPC Python.  gRPC Python wraps
gRPC core, which uses multithreading for performance, and hence doesn't support ```fork()```.
Historically, we didn't support forking in gRPC, but some users seemed
to be doing fine until their code started to break on version 1.6.  This was
likely caused by the addition of background c-threads and a background
Python thread.

# Current Status #

## 1.11 ##
The background Python thread was removed entirely.  This allows forking
after creating a channel.  However, the channel must not have issued any
RPCs prior to the fork.  Attempting to fork with an active channel that
has been used can result in deadlocks/corrupted wire data.

## 1.9 ##
A regression was noted in cases where users are doing fork/exec. This
was due to ```pthread_atfork()``` handler that was added in 1.7 to partially
support forking in gRPC. A deadlock can happen when pthread_atfork
handler is running, and an application thread is calling into gRPC.
We have provided a workaround for this issue by allowing users to turn
off the handler using env flag ```GRPC_ENABLE_FORK_SUPPORT=False```.
This should be set whenever a user expects to always call exec
immediately following fork.  It will disable the fork handlers.

## 1.7 ##
A ```pthread_atfork()``` handler was added in 1.7 to automatically shut down
the background c-threads when fork was called.  This does not shut down the
background Python thread, so users could not have any open channels when
forking.

# Future Work #

## 1.13 ##
The workaround when using fork/exec by setting
```GRPC_ENABLE_FORK_SUPPORT=False``` should no longer be needed.  Following
[this PR](https://github.com/grpc/grpc/pull/14647), fork
handlers will not automatically run when multiple threads are calling
into gRPC.
