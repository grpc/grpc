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

We are temporarily using an additionl tag `"cpp_end2end_test_client_ph2"`
to gradually enable the `"cpp_end2end_test"` suite for PH2 target by target.
Once all targets are enabled for PH2, remove `"cpp_end2end_test_client_ph2"`,
and add `"cpp_end2end_test"` tag to the PH2 experiment.
ETA : 30-Jan-2025. Owner : tjagtap
