# gRPC C++ Gemini Collaboration Guide

This document outlines conventions and best practices for AI-assisted development in the gRPC C++ codebase.

## Preferred Tools & Libraries
*   Prefer gRPC types before absl.
*   gRPC uses C++17, so we can't use C++20 onwards types.
*   The Python implementation cannot depend on the protobuf library, so any shared libraries must expose a C-style API that does not rely on C++ protobuf types.

## Code Style & Conventions
*   `#include <grpc/support/port_platform.h>` is not required unless its macros are needed for compilation.
*   Abseil headers are sorted before gRPC headers.
*   For public api headers (in `include/grpc`) we use `<grpc/...>`.
    *   Example: `#include <grpc/grpc.h>`
*   Prefer explicit types over `std::pair` or `std::tuple` for return types.
*   Use `LOG(ERROR)` from `absl/log/log.h` for logging errors, do
    not use `std::cerr` or `gpr_log`.

## About gRPC
*   gRPC uses its own macros for Bazel libraries, tests, etc. Each directory should have a `grpc_package` declaration. For libraries use `grpc_cc_library`, for tests `grpc_cc_test`.
*   Dependencies on non-gRPC libraries (like gtest or absl) are listed in the `external_deps` attribute.
*   The `:grpc` BUILD target is not allowed to depend on the C++ protobuf library.
*   Build files for implementation code are typically located in `src/core/BUILD.bazel` and `BUILD.bazel`. Do not add new `BUILD.bazel` files under the `src/` tree without explicit instruction.
*   Tests are located in `test/core` and `test/cpp` (corresponding to the `src` directories). These test directories contain their own `BUILD.bazel` files.
*   Fuzz tests use `fuzztest_main` instead of `gtest_main`.
*   When depending on a `grpc_proto_library`, the name of the `cc_library` target is the same as the `name` of the `grpc_proto_library` rule itself, not `[name]_cc_proto` as one might expect from standard Bazel `cc_proto_library` rules. The build system error messages can be misleading in this case.
*   The 'gtest' external_dep also includes 'gmock'.
*   All `upb` related build rules (`grpc_upb_proto_library`, `grpc_upb_proto_reflection_library`) for protos defined anywhere in the repository must be defined in the root `BUILD.bazel` file. They should not be placed in the `BUILD.bazel` file of the subdirectory where the proto is located.
*   Core end-to-end tests are defined in `test/core/end2end/BUILD.bazel` using the `grpc_core_end2end_test_suite` macro. This macro generates multiple `grpc_cc_test` targets by combining a configuration file (like `end2end_http2_config.cc`) with individual test implementation files located in `test/core/end2end/tests/`. The final test target name is created by appending `_test` to the `name` attribute of the macro. For example, `grpc_core_end2end_test_suite(name = "end2end_http2", ...)` generates the test target `//test/core/end2end:end2end_http2_test`.
*   Unused named parameters is a compilation failure.
