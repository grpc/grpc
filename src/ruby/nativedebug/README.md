This package contains debug symbols that can be useful for debugging
applications that use grpc pre-compiled binary gems.

An example of a pre-compiled binary gem is `grpc-1.58.0-x86_64-linux.gem`
(as opposed to a source-built gem like `grpc-1.58.0.gem`).

`grpc-native-debug` gems contain debug symbols which complement the
native libraries in these grpc binary gems. After fetching and unpacking a
proper `grpc-native-debug` gem, one can load the correct `.dbg` symbol file to
debug their grpc application.

# Background

grpc-ruby pre-compiled binary gems are *released with debug symbols stripped*.
As a consequence, if you are to examine a grpc stack trace in a debugger
for example, a lot of information will initially be missing.

# Using grpc-native-debug

## Finding the correct grpc-native-debug gem

Each `grpc-native-debug` gem is *one-to-one* with a `grpc` gem. Specifically:

- The version of a `grpc-native-debug` gem **must match the version** of the `grpc`
  gem.

- The ruby platform of a `grpc-native-debug` gem **must match the ruby platform** of
  the `grpc` gem.

So for example, if you are debugging `grpc-1.60.1-x86_64-linux.gem`, then you
need to fetch `grpc-native-debug-1.60.1-x86_64-linux.gem`.

## Finding the correct .dbg symbol file

Each `grpc-native-debug` gem has a top-level `symbols` directory containing
symbol files ending in `.dbg`.

`grpc` binary gems are shipped with multiple native libraries. There is one
native library for each supported *minor version* of ruby. As such,
`grpc-native-debug` gems have exactly one `.dbg` file for each native library
in the corresponding `grpc` gem.

If you unpack a `grpc-native-debug` gem and look at the `symbols`
directory, you might see something like this:

```
grpc-native-debug-1.60.1-x86_64-linux/symbols/grpc-1.60.1-x86_64-linux-ruby-3.0.dbg
grpc-native-debug-1.60.1-x86_64-linux/symbols/grpc-1.60.1-x86_64-linux-ruby-2.7.dbg
grpc-native-debug-1.60.1-x86_64-linux/symbols/grpc-1.60.1-x86_64-linux-ruby-3.2.dbg
grpc-native-debug-1.60.1-x86_64-linux/symbols/grpc-1.60.1-x86_64-linux-ruby-3.1.dbg
```

In each of these `.dbg` files, the `ruby-<major>-<minor>` portion of the string
indicates which ruby version it's supposed to be used with.

So for example, if you are debugging `grpc-1.60.1-x86_64-linux.gem` on ruby-3.0, then you
need to use symbol file
`grpc-native-debug-1.60.1-x86_64-linux/symbols/grpc-1.60.1-x86_64-linux-ruby-3.0.dbg`.

## Putting symbols into action (example gdb workflow)

There are a variety of ways to use these symbols.

As a toy example, suppose we are running an application under gdb using:

- ruby-3.0

- grpc-1.60.1.x86_64-linux.gem

At first, in gdb we might dump a grpc-ruby stack trace looking
something like this:

```
(gdb) bt
#0  0x00007ffff7926e56 in epoll_wait (epfd=5, events=0x7ffff3cb4144, maxevents=100, timeout=-1) at ../sysdeps/unix/sysv/linux/epoll_wait.c:30
#1  0x00007ffff383eb9e in ?? () from /home/.rvm/gems/ruby-3.0.0/gems/grpc-1.60.1-x86_64-linux/src/ruby/lib/grpc/3.0/grpc_c.so
#2  0x00007ffff355e002 in ?? () from /home/.rvm/gems/ruby-3.0.0/gems/grpc-1.60.1-x86_64-linux/src/ruby/lib/grpc/3.0/grpc_c.so
#3  0x00007ffff38466e2 in ?? () from /home/.rvm/gems/ruby-3.0.0/gems/grpc-1.60.1-x86_64-linux/src/ruby/lib/grpc/3.0/grpc_c.so
#4  0x00007ffff35ba2ea in ?? () from /home/.rvm/gems/ruby-3.0.0/gems/grpc-1.60.1-x86_64-linux/src/ruby/lib/grpc/3.0/grpc_c.so
#5  0x00007ffff34abf6b in ?? () from /home/.rvm/gems/ruby-3.0.0/gems/grpc-1.60.1-x86_64-linux/src/ruby/lib/grpc/3.0/grpc_c.so
#6  0x00007ffff7c67ca7 in rb_nogvl (func=0x7ffff34abed3, data1=0x0, ubf=<optimized out>, data2=<optimized out>, flags=<optimized out>) at thread.c:1669
#7  0x00007ffff34ab110 in ?? () from /home/.rvm/gems/ruby-3.0.0/gems/grpc-1.60.1-x86_64-linux/src/ruby/lib/grpc/3.0/grpc_c.so
#8  0x00007ffff7c6780c in thread_do_start (th=0x555555ad16e0) at thread.c:769
#9  thread_start_func_2 (th=th@entry=0x555555ad16e0, stack_start=<optimized out>) at thread.c:822
#10 0x00007ffff7c679a6 in thread_start_func_1 (th_ptr=<optimized out>) at /home/.rvm/src/ruby-3.0.0/thread_pthread.c:994
#11 0x00007ffff78a63ec in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:444
#12 0x00007ffff7926a4c in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:81
```

We could take the following steps to get more debug info:

<h3>1) Fetch the correct grpc-native-debug gem</h3>

```
cd /home
gem fetch grpc-native-debug-1.60.1.x86_64-linux.gem
gem unpack grpc-native-debug-1.60.1.x86_64-linux.gem
```

(note again the version and platform of `grpc-native-debug` must match the `grpc` gem)

<h3>2) Load debug symbols (for ruby-3.0)</h3>

```
(gdb) info sharedlibrary
From                To                  Syms Read   Shared Object Library
...
0x00007ffff3497450  0x00007ffff3a61912  Yes (*)     /home/.rvm/gems/ruby-3.0.0/gems/grpc-1.60.1-x86_64-linux/src/ruby/lib/grpc/3.0/grpc_c.so
0x00007ffff3e78730  0x00007ffff3ea60df  Yes (*)     /home/.rvm/gems/ruby-3.0.0/gems/google-protobuf-3.24.4-x86_64-linux/lib/google/3.0/protobuf_c.so
(*): Shared library is missing debugging information.
(gdb) add-symbol-file /home/grpc-native-debug-1.60.1-x86_64-linux/symbols/grpc-1.60.1-x86_64-linux-ruby-3.0.dbg 0x00007ffff3497450
add symbol table from file "/home/grpc-native-debug-1.60.1-x86_64-linux/symbols/grpc-1.60.1-x86_64-linux-ruby-3.0.dbg" at
	.text_addr = 0x7ffff3497450
(y or n) y
Reading symbols from /home/grpc-native-debug-1.60.1-x86_64-linux/symbols/grpc-1.60.1-x86_64-linux-ruby-3.0.dbg...
(gdb)
```

Our stack trace might look more like this now:

```
(gdb) bt
#0  0x00007ffff7926e56 in epoll_wait (epfd=5, events=0x7ffff3cb4144, maxevents=100, timeout=-1) at ../sysdeps/unix/sysv/linux/epoll_wait.c:30
#1  0x00007ffff383eb9e in do_epoll_wait (ps=0x555555ad1690, deadline=...) at src/core/lib/iomgr/ev_epoll1_linux.cc:723
#2  pollset_work (ps=0x555555ad1690, worker_hdl=0x0, deadline=...) at src/core/lib/iomgr/ev_epoll1_linux.cc:1038
#3  0x00007ffff355e002 in pollset_work (pollset=<optimized out>, worker=<optimized out>, deadline=...) at src/core/lib/iomgr/ev_posix.cc:249
#4  0x00007ffff38466e2 in grpc_pollset_work (pollset=<optimized out>, worker=<optimized out>, deadline=...) at src/core/lib/iomgr/pollset.cc:48
#5  0x00007ffff35ba2ea in cq_next (cq=0x555555ad1510, deadline=..., reserved=<optimized out>) at src/core/lib/surface/completion_queue.cc:1043
#6  0x00007ffff34abf6b in run_poll_channels_loop_no_gil (arg=arg@entry=0x0) at ../../../../src/ruby/ext/grpc/rb_channel.c:663
#7  0x00007ffff7c67ca7 in rb_nogvl (func=0x7ffff34abed3 <run_poll_channels_loop_no_gil>, data1=0x0, ubf=<optimized out>, data2=<optimized out>, flags=flags@entry=0) at thread.c:1669
#8  0x00007ffff7c68138 in rb_thread_call_without_gvl (func=<optimized out>, data1=<optimized out>, ubf=<optimized out>, data2=<optimized out>) at thread.c:1785
#9  0x00007ffff34ab110 in run_poll_channels_loop (arg=<optimized out>) at ../../../../src/ruby/ext/grpc/rb_channel.c:734
#10 0x00007ffff7c6780c in thread_do_start (th=0x555555ad16e0) at thread.c:769
#11 thread_start_func_2 (th=th@entry=0x555555ad16e0, stack_start=<optimized out>) at thread.c:822
#12 0x00007ffff7c679a6 in thread_start_func_1 (th_ptr=<optimized out>) at /home/.rvm/src/ruby-3.0.0/thread_pthread.c:994
#13 0x00007ffff78a63ec in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:444
#14 0x00007ffff7926a4c in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:81
```

This is better, but if we try to examine a frame closely we'll notice
that source file information is still missing:

```
(gdb) up
#1  0x00007ffff383eb9e in do_epoll_wait (ps=0x555555ad1690, deadline=...) at src/core/lib/iomgr/ev_epoll1_linux.cc:723
723	src/core/lib/iomgr/ev_epoll1_linux.cc: No such file or directory.
(gdb) list
718	in src/core/lib/iomgr/ev_epoll1_linux.cc
(gdb)
```

<h3>3) Resolve source files</h3>

First, we fetch the *source* `grpc` gem at the **exact same version** of our binary
`grpc` gem:

```
cd /home
gem fetch grpc-1.60.1.gem
gem unpack grpc-1.60.1.gem
```

Now we can load those sources in gdb:

```
(gdb) dir /home/grpc-1.60.1
Source directories searched: /home/grpc-1.60.1:$cdir:$cwd
(gdb)
```

Our stack frame will might look more like this now:

```
(gdb) list
warning: Source file is more recent than executable.
718	  int timeout = poll_deadline_to_millis_timeout(deadline);
719	  if (timeout != 0) {
720	    GRPC_SCHEDULING_START_BLOCKING_REGION;
721	  }
722	  do {
723	    r = epoll_wait(g_epoll_set.epfd, g_epoll_set.events, MAX_EPOLL_EVENTS,
724	                   timeout);
725	  } while (r < 0 && errno == EINTR);
726	  if (timeout != 0) {
727	    GRPC_SCHEDULING_END_BLOCKING_REGION;
(gdb)
```

But if we move up a few stack frames we might *still* be missing
some source information:

```
(gdb) up
#6  0x00007ffff34abf6b in run_poll_channels_loop_no_gil (arg=arg@entry=0x0) at ../../../../src/ruby/ext/grpc/rb_channel.c:663
663	../../../../src/ruby/ext/grpc/rb_channel.c: No such file or directory.
```

A portion of the grpc-ruby native extension is built from a sub-directory:
`src/ruby/ext/grpc`. So we also need to add that sub-directory, to fix this:

```
(gdb) dir /home/grpc-1.60.1/src/ruby/ext/grpc
Source directories searched: /home/grpc-1.60.1/src/ruby/ext/grpc:/home/grpc-1.60.1:$cdir:$cwd
```

Note the additional info:

```
(gdb) list
warning: Source file is more recent than executable.
658	  gpr_mu_lock(&global_connection_polling_mu);
659	  gpr_cv_broadcast(&global_connection_polling_cv);
660	  gpr_mu_unlock(&global_connection_polling_mu);
661
662	  for (;;) {
663	    event = grpc_completion_queue_next(
664	        g_channel_polling_cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
665	    if (event.type == GRPC_QUEUE_SHUTDOWN) {
666	      break;
667	    }
(gdb)
```

# Support

grpc-native-debug currently only supports:

- ruby platforms: `x86_64-linux` and `x86-linux`

- grpc >= 1.60.0
