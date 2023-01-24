A reusable test suite for EventEngine implementations.

# Customizing tests for your EventEngine implementation

To exercise a custom EventEngine, create a new bazel test target that links
against some set of targets in the `//test/core/event_engine/test_suite/tests/...` 
folder, and provide a testing `main` function that sets a custom EventEngine factory.

Your custom test target will look something like:

```
grpc_cc_test(
    name = "my_custom_event_engine_test",
    srcs = ["my_custom_event_engine_test.cc"],
    uses_polling = False,
    deps = ["//test/core/event_engine/test_suite/tests:timer"],
)
```

And the main function will be similar to:

```
#include "path/to/my_custom_event_engine.h"
#include "test/core/event_engine/test_suite/event_engine_test_framework.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  SetEventEngineFactory(
      []() { return absl::make_unique<MyCustomEventEngine>(); });
  auto result = RUN_ALL_TESTS();
  return result;
}
```

Alternatively, if you only want to exercise a subset of the conformance tests,
you could depend on any subset of the following:

* `//test/core/event_engine/test_suite/tests:timer`
* `//test/core/event_engine/test_suite/tests:dns`
* `//test/core/event_engine/test_suite/tests:client`
* `//test/core/event_engine/test_suite/tests:server`

# Useful testing tools

The suite also provides [tools](tools/) that let you exercise your custom EventEngine in other ways. 
For example, the `echo_client` library allows you to prop up a TCP echo client based on your EventEngine::Connect and ::Endpoint implementations, and communicate with a remote TCP listener of your choosing.

You'll need to provide the following code

```
# tools/BUILD:
grpc_cc_binary(
    name = "my_event_engine_echo_client",
    srcs = ["my_event_engine_factory.cc"],
    deps = ["echo_client"],
)

# tools/my_event_engine_factory.cc: an implementation of CustomEventEngineFactory
absl::AnyInvocable<
    std::unique_ptr<grpc_event_engine::experimental::EventEngine>(void)>
CustomEventEngineFactory() {
  return []() {
    return std::make_unique<
        grpc_event_engine::experimental::WindowsEventEngine>();
  };
}
```

To exercise the echo client, run `bazel run //test/core/event_engine/test_suite/tools:my_event_engine_echo_client`, and in a separate terminal, open something like netcat to run a TCP listener and communicate with the client.

Each tool is documented more fully in its source file.
