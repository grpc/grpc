# FilterTestV3 — native v3 filter & interceptor test suite

`FilterTestV3` is a unit-test harness for **v3 call filters and interceptors**.
It runs the filter(s) under test on the *real* v3 stack —
`InterceptionChainBuilder` → `CallFilters` → `CallSpine` — with a live
client/server call pair, so the six lifecycle hooks fire through the genuine
executor exactly as they do in production. Because it is built on the
[yodel](../../call/yodel/README.md) harness, every test is also a fuzz target.

## When to use this vs. `FilterTest<Filter>`

* Use **`FilterTestV3`** (this suite) for v3 filters/interceptors: it exercises
  the native `CallSpine`, supports multi-filter chains and interceptors
  (hijack / consume / passthrough), and fuzzes for free.
* The older `FilterTest<Filter>` (`test/core/filters/filter_test.h`) drives a
  single filter through the v2 `MakeCallPromise` bridge on a synthetic pipe rig.
  It remains for existing tests; prefer `FilterTestV3` for new work.

## The model

```
CallInitiator (client)  ──►  [ filters / interceptors under test ]  ──►  server
        ▲                                                                  │
        └── the test drives this end ──┐         ┌── the test drives this ─┘
                                   SpawnTestSeq(initiator, …)   SpawnTestSeq(handler, …)
```

* `StartCall(md)` starts a call and returns the client-side `CallInitiator`.
* `TickUntilServerCall()` returns the server-side `CallHandler` once the call
  has traversed the stack and reached the terminal server destination.
* You drive **each end** with its own `SpawnTestSeq(...)` chain, then call
  `WaitForAllPendingWork()`.

## Two rules that will bite you

1. **Filters run lazily — on pull.** A v3 hook fires only when the *peer* pulls
   that event. `OnClientInitialMetadata` runs when the server calls
   `PullClientInitialMetadata`; `OnClientToServerMessage` runs when the server
   pulls the message. **You must drive both ends** — a client-only script never
   runs the filter at all.

2. **Order server pulls before server pushes.** If the client blocks pushing a
   message while the server blocks pushing its metadata, you get a cyclic wait
   and the test times out. Have the server **pull the client's message before
   producing its own output**. See `message_flow_test.cc` for the canonical
   shape.

Both ends must be driven as *concurrent* `SpawnTestSeq` chains — a single
blocking driver on one end would wait on the other and deadlock. Party
discipline (initiator ops on the initiator party, handler ops on the handler
party) falls out of this automatically and is verified under TSan.

## Writing a test

```cpp
#include "test/core/filters/test_suite/filter_test.h"
#include "test/core/filters/test_suite/filter_matchers.h"

FILTER_TEST_V3(MyFilterStampsHeader) {
  // 1. Build the stack. Add<T>() takes a filter OR an Interceptor.
  ASSERT_TRUE(Add<MyFilter>().Build(channel_args).ok());

  // 2. Start the call and drive the client end.
  auto initiator = StartCall(NewClientMetadata({{"k", "v"}}));
  SpawnTestSeq(
      initiator, "client",
      [initiator]() mutable { return initiator.PullServerTrailingMetadata(); },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_THAT(**md, HasMetadataResult(absl::OkStatus()));
      });

  // 3. Drive the server end (this is what makes the filter run).
  auto handler = TickUntilServerCall();
  SpawnTestSeq(
      handler, "server",
      [handler]() mutable { return handler.PullClientInitialMetadata(); },
      [handler](ValueOrFailure<ClientMetadataHandle> md) mutable {
        EXPECT_THAT(**md, HasMetadataKeyValue("stamped", "yes"));
        handler.PushServerTrailingMetadata(
            ServerMetadataFromStatus(GRPC_STATUS_OK));
      });

  WaitForAllPendingWork();
}
```

### Fixture API

| Member | Purpose |
| --- | --- |
| `Add<T>(config = nullptr)` | Queue a filter or `Interceptor` (chainable). |
| `Build(args = {})` | Build the chain onto the server destination; returns `absl::Status`. |
| `StartCall(md)` | Start a call; returns the client `CallInitiator`. |
| `TickUntilServerCall()` | Return the server `CallHandler` once the call arrives. |
| `NewClientMetadata({…})` / `NewServerMetadata({…})` | Build metadata with key/value pairs. |
| `NewMessage(payload, flags)` | Build a message. |

### Matchers (`filter_matchers.h`)

`HasMetadataKeyValue(k, v)`, `LacksMetadataKey(k)`, `HasMessagePayload(v)`,
`HasMessageFlags(f)`, `HasMetadataResult(absl::Status)`.

## Tests in this suite

| File | Demonstrates |
| --- | --- |
| `client_authority_test.cc` | Metadata mutation by a real filter (`ClientAuthorityFilter`). |
| `message_flow_test.cc` | All six lifecycle events (unary echo through a pass-through filter). |
| `message_size_test.cc` | A real filter processing a message (`ServerMessageSizeFilter`). |
| `filter_abort_test.cc` | Failure path: a filter aborts a call at initial metadata. |
| `interceptor_test.cc` | Interceptors: passthrough / consume / hijack. |
| `chain_test.cc` | Two real filters composed in one chain. |

## Running

```sh
# Unit mode (also runs a short fuzzing burst):
bazel test //test/core/filters/test_suite/...

# With ThreadSanitizer:
bazel test --config=tsan //test/core/filters/test_suite/...

# Deep coverage-guided fuzzing of one test:
bazel run --config=fuzztest //test/core/filters/test_suite:message_flow_test \
    -- --fuzz=FilterTestV3.UnaryEchoThroughPassThroughFilter --fuzz_for=60s
```

Fuzztest is domain-based (inputs come from `yodel/fuzzer.proto`); there is no
libFuzzer-style seed corpus to check in.
