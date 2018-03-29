# Background #

In gRPC python, multithreading is not usable due to GIL
(global interpreter lock).Users are using multiprocessing and
concurrent.futures module to accomplish multiprocessing.  These modules fork
processes underneath. Various issues have been reported when using these
modules.  Historically, we didn't support forking in gRPC, but some users seem
to be doing fine until their code started to break 1.6.  This was
likely caused by the addition of background c-threads and a background
Python thread.

# Current Status #
## 1.7 ##
A pthread_atfork() handler was added in 1.7 to automatically shut down
the background c-threads when fork was called.  This does not shut down the
background Python thread, so users could not have any open channels when
forking().

## 1.9 ##
A regression was noted in cases where users are doing fork/exec. This
was due to pthread_atfork() handler that was added in 1.7 to partially
support forking in gRPC. A deadlock can happen around GIL when pthread_atfork
handler is holding the lock while another thread is blocked on it and the
handler is waiting for that thread to terminate. We have provided a workaround
for this issue by allowing users to turn off the handler using env flag
```GRPC_ENABLE_FORK_SUPPORT=False```.  This should be set whenever a user expects
to always call exec immediately following fork.  It will disable the fork
handlers.

# Future Work #
## 1.11 ##
The background Python thread was removed entirely.  This allows forking
after creating a channel.  However, the channel cannot be used by both the
parent and child process after the fork.

Additionally, the fork/exec workaround of setting
```GRPC_ENABLE_FORK_SUPPORT=False``` should no longer be needed.  Now the fork
handlers are automatically not run when multiple threads are calling
into gRPC.


## 1.1x ##
We would like to support forking() and using the channel from both the parent
and child process.  Additionally, we would like to support servers that
use a prefork model, where the child processes accept the connections
and handle requests.
