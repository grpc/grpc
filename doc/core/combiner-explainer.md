# Combiner Explanation
## Talk by ctiller, notes by vjpai

Typical way of doing critical section

```
mu.lock()
do_stuff()
mu.unlock()
```

An alternative way of doing it is

```
class combiner {
  run(f) {
    mu.lock()
    f()
    mu.unlock()
  }
  mutex mu;
}

combiner.run(do_stuff)
```

If you have two threads calling combiner, there will be some kind of
queuing in place. It's called `combiner` because you can pass in more
than one do_stuff at once and they will run under a common `mu`.

The implementation described above has the issue that you're blocking a thread
for a period of time, and this is considered harmful because it's an application thread that you're blocking.

Instead, get a new property:
* Keep things running in serial execution
* Don't ever sleep the thread
* But maybe allow things to end up running on a different thread from where they were started
* This means that `do_stuff` doesn't necessarily run to completion when `combiner.run` is invoked

```
class combiner {
  mpscq q; // multi-producer single-consumer queue can be made non-blocking
  state s; // is it empty or executing

  run(f) {
    if (q.push(f)) {
      // q.push returns true if it's the first thing
      while (q.pop(&f)) { // modulo some extra work to avoid races
        f();
      }
    }
  }
}
```

The basic idea is that the first one to push onto the combiner
executes the work and then keeps executing functions from the queue
until the combiner is drained.

Our combiner does some additional work, with the motivation of write-batching.

We have a second tier of `run` called `run_finally`. Anything queued
onto `run_finally` runs after we have drained the queue. That means
that there is essentially a finally-queue. This is not guaranteed to
be final, but it's best-effort. In the process of running the finally
item, we might put something onto the main combiner queue and so we'll
need to re-enter.

`chttp2` runs all ops in the run state except if it sees a write it puts that into a finally. That way anything else that gets put into the combiner can add to that write.

```
class combiner {
  mpscq q; // multi-producer single-consumer queue can be made non-blocking
  state s; // is it empty or executing
  queue finally; // you can only do run_finally when you are already running something from the combiner

  run(f) {
    if (q.push(f)) {
      // q.push returns true if it's the first thing
      loop:
      while (q.pop(&f)) { // modulo some extra work to avoid races
        f();
      }
      while (finally.pop(&f)) {
        f();
      }
      goto loop;
    }
  }
}
```

So that explains how combiners work in general. In gRPC, there is
`start_batch(..., tag)` and then work only gets activated by somebody
calling `cq::next` which returns a tag. This gives an API-level
guarantee that there will be a thread doing polling to actually make
work happen. However, some operations are not covered by a poller
thread, such as cancellation that doesn't have a completion. Other
callbacks that don't have a completion are the internal work that gets
done before the batch gets completed. We need a condition called
`covered_by_poller` that means that the item will definitely need some
thread at some point to call `cq::next` . This includes those
callbacks that directly cause a completion but also those that are
indirectly required before getting a completion. If we can't tell for
sure for a specific path, we have to assumed it is not covered by
poller.

The above combiner has the problem that it keeps draining for a
potentially infinite amount of time and that can lead to a huge tail
latency for some operations. So we can tweak it by returning to the application
if we know that it is valid to do so:

```
while (q.pop(&f)) {
  f();
  if (control_can_be_returned && some_still_queued_thing_is_covered_by_poller) {
    offload_combiner_work_to_some_other_thread();
  }
}
```

`offload` is more than `break`; it does `break` but also causes some
other thread that is currently waiting on a poll to break out of its
poll. This is done by setting up a per-polling-island work-queue
(distributor) wakeup FD. The work-queue is the converse of the combiner; it
tries to spray events onto as many threads as possible to get as much concurrency as possible.

So `offload` really does:

```
  workqueue.run(continue_from_while_loop);
  break;
```

This needs us to add another class variable for a `workqueue`
(which is really conceptually a distributor).

```
workqueue::run(f) {
  q.push(f)
  eventfd.wakeup()
}

workqueue::readable() {
  eventfd.consume();
  q.pop(&f);
  f();
  if (!q.empty()) {
    eventfd.wakeup(); // spray across as many threads as are waiting on this workqueue
  }
}
```

In principle, `run_finally` could get starved, but this hasn't
happened in practice. If we were concerned about this, we could put a
limit on how many things come off the regular `q` before the `finally`
queue gets processed.

