# gRPC C++ End-to-End Tests

This directory contains end-to-end tests for the gRPC C++ library.
These tests cover the full gRPC stack,
from the C++ API down to the core transport and back.

## Key Test Files

*   **`end2end_test.cc`**: The main end-to-end test suite. It covers basic RPC functionality, cancellation, deadlines, metadata, and various channel/server configurations.
*   **`async_end2end_test.cc`**: Tests using the asynchronous C++ API.
*   **`client_callback_end2end_test.cc`**: Tests using the callback-based asynchronous API.
*   **`generic_end2end_test.cc`**: Tests for the generic (byte-stream) API.
*   **`hybrid_end2end_test.cc`**: Tests mixing sync and async APIs.
*   **`interceptors_util.{h,cc}`**: Utilities for testing interceptors.
*   **`test_service_impl.{h,cc}`**: Implementation of the test service used in many end-to-end tests.

## Common Patterns

### Test Scenarios

Tests often use `::testing::TestWithParam<TestScenario>` to run the same test
logic under different configurations. A `TestScenario` typically includes:

*   **Interceptors**: Whether to use client/server interceptors.
*   **Proxy**: Whether to route traffic through a proxy.
*   **Inproc**: Whether to use the in-process transport.
*   **Credentials**: The type of credentials to use (Insecure, TLS, etc.).
*   **Callback Server**: Whether to use the callback-based server API.

### Service Implementation

The `TestServiceImpl` class (defined in `test_service_impl.h`) provides a
standard implementation of the `EchoTestService` for use in tests.
It supports features such as:

*   Echoing request messages and metadata.
*   Simulating server-side errors.
*   Checking for cancellation.

### Server and Channel Creation

Tests typically use helper methods like `BuildAndStartServer` and
`ResetChannel`/`ResetStub` to set up the test environment based on the current
`TestScenario` parameters.

### BUILD File Tags

In the `BUILD` file, targets intended to be part of the core C++ end-to-end
test suite are typically tagged with `"cpp_end2end_test"`.
Reference for PH2 and cpp_end2end_test : src/core/lib/experiments/experiments.yaml

We have temporarily used SKIP_TEST_FOR_PH2 to mark tests that are failing for
PH2 experiment, and are WIP.
ETA to finish : 30-Feb-2026. Owner : tjagtap
For more information on PH2 refer file src/core/ext/transport/chttp2/GEMINI.md

### Retry for PH2

Retries have not yet been implemented for PH2.
Hence, whenever PH2 experiment `IsPromiseBasedHttp2ClientTransportEnabled()`is
enabled, we must disable retries. This is done with ApplyCommonChannelArguments.
file : test/cpp/end2end/end2end_test_utils.h
