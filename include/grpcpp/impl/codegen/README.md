# Welcome to `include/grpcpp/impl/codegen`

## Why is this directory here?

This directory exists so that generated code can include selected files upon
which it depends without having to depend on the entire gRPC C++ library. This
is particularly relevant for users of bazel, particularly if they use the
multi-lingual `proto_library` target type. Generated code that uses this target
only depends on the gRPC C++ targets associated with these header files, not the
entire gRPC C++ codebase since that would make the build time of these types of
targets excessively large (particularly when they are not even C++ specific).

## What should user code do?

User code should *not* include anything from this directory. Only generated code
and gRPC library code should include contents from this directory. User code
should instead include contents from the main `grpcpp` directory or its
accessible subcomponents like `grpcpp/support`. It is possible that we may
remove this directory altogether if the motivations for its existence are no
longer strong enough (e.g., if most users migrate away from the `proto_library`
target type or if the additional overhead of depending on gRPC C++ is not high).
