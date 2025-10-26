# gRPC Promise Library

This directory contains the implementation of gRPC's promise library, a framework for asynchronous programming.

## Overarching Purpose

The promise library provides a set of tools for writing asynchronous code in a composable and easy-to-understand way. It is based on the concept of a "promise," which is an object that represents the eventual result of an asynchronous operation. Promises are polled to advance their state, and can be chained together to form complex asynchronous workflows. They are designed to be lightweight and efficient, with a focus on minimizing memory allocations and avoiding virtual function calls.

## Files and Subdirectories

### Core Promise Components

- **`promise.h`**: Defines the core `Promise` class and related helpers. A `Promise` is a functor that returns a `Poll<T>`. Most code will not use Promise<T> directly, as it's type erased and introduces unnecessary indirect function calls and memory allocations.
- **`poll.h`**: Defines `Poll<T>`, the return type of a promise. A `Poll` can be either `Pending` or `Ready` with a value of type `T`.
- **`party.h` / `party.cc`**: A `Party` is a tool that we use to execute promises concurrently not parallely.
- **`activity.h` / `activity.cc`**: Defines `Activity`, the execution context for promises. An `Activity` is responsible for running a promise to completion, and provides a mechanism for waking up a suspended promise when it's ready to make progress.

### Promise Combinators

Promise combinators are designed to be composable and can be safely nested to build more complex asynchronous logic.

- Conditionals
  - **`if.h`**: `If` is a combinator that conditionally executes one of two promises. It can use a promise or a boolean as a predicate.
  - **`switch.h`**: `Switch` is a combinator that allows for switching between different promises based on a condition.
- Join
  - **`join.h`**: `Join` is a combinator that takes a number of promises and returns a new promise that resolves to a tuple of the results when all of the input promises have resolved.
  - **`try_join.h`**: `TryJoin` is a combinator that is similar to `Join`, but it resolves with an error if any of the input promises resolve with an error.
- Loop
  - **`loop.h`**: `Loop` is a combinator that repeatedly executes a promise.
  - **`for_each.h`**: `ForEach` is a combinator that iterates over a sequence and applies a function to each element, returning a promise that resolves when all of the function calls have completed.
- **`map.h`**: `Map` is a combinator that applies a synchronous function to the result of a promise.
- **`match_promise.h`**: `Match` is a combinator that allows for pattern matching on the resolved type of a promise.
- Races
  - **`race.h`**: `Race` is a combinator that races multiple promises against each other.
  - **`prioritized_race.h`**: `PrioritizedRace` is a combinator that races multiple promises against each other, but with a priority given to the first promise.
  - All raced promises must resolve to the same type.
- Sequences `Seq`
  - **`seq.h`**: `Seq` is a combinator that executes a sequence of promises one after another.  It resolves to the return type of the last promise in the sequence.
  - **`try_seq.h`**: `TrySeq` is a combinator that is similar to `Seq`, but it stops executing if any of the promises in the sequence resolve with an error.
- **`all_ok.h`**: `AllOk` is a combinator that takes a number of promises and returns a new promise that resolves when all of the input promises have resolved successfully.

### Synchronization Primitives

- **`inter_activity_latch.h`**: A latch that can be used to synchronize between different activities.
- **`inter_activity_mutex.h`**: A mutex that can be used to protect shared data between different activities.
- **`latch.h`**: A simple latch that can be used for synchronization within a single activity.
- **`promise_mutex.h`**: A mutex that can be used to protect shared data within a single activity.

### Other Files

- **`arena_promise.h`**: `ArenaPromise<T>` is a promise that is allocated from an arena, which was be useful for performance-critical code. It is *deprecated* and should not be used in new code
- **`context.h`**: `GetContext<T>()` allows retrieving a `T` from the current activity's context.
- **`detail/`**: Implementation details for the promise library.
- **`event_engine_wakeup_scheduler.h`**: A wakeup scheduler that uses the `EventEngine` to schedule wakeups.
- **`exec_ctx_wakeup_scheduler.h`**: A wakeup scheduler that uses the `ExecCtx` to schedule wakeups.
- **`inter_activity_pipe.h`**: A pipe that can be used for communication between different activities.
- **`interceptor_list.h`**: A list of interceptors that can be used to add functionality to a promise.
- **`mpsc.h` / `mpsc.cc`**: A multi-producer, single-consumer queue that can be used for communication between promises.
- **`observable.h`**: `Observable` is a promise that can be observed by multiple other promises.
- **`pipe.h`**: `Pipe` provides a mechanism for communicating between two promises within the same activity.
- **`sleep.h` / `sleep.cc`**: `Sleep` is a promise that resolves after a given duration.
- **`status_flag.h`**: A simple flag that can be used to indicate the status of an asynchronous operation.
- **`wait_for_callback.h`**: `WaitForCallback` is a promise that waits for a callback to be called.
- **`wait_set.h` / `wait_set.cc`**: A set of promises that can be waited on together.

## Notes

- The promise library is a key component of gRPC's asynchronous programming model.
- It is used extensively throughout the gRPC core to implement non-blocking I/O and other asynchronous operations.
- The library provides a rich set of combinators for composing promises together to create complex asynchronous workflows.
- Pay close attention to the distinction between inter-activity and intra-activity synchronization primitives. Using the wrong one can lead to deadlocks.
- The use of `ArenaPromise` should be considered in performance-sensitive code to avoid heap allocations.
- The `detail/` directory contains internal implementation details and should not be used directly by consumers of the library.

## Test Files

- The test files for this library are located in `test/core/promise/`.
