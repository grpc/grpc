## Bazel Workspace Test (gRPC Python)

This directory houses a test ensuring that downstream projects can use
`@com_github_grpc_grpc//src/python/grpcio:grpcio`, `py_proto_library`, and
`py_grpc_library`.

To run the test (with currently checked-out version of grpc/grpc)
```
# from this directory run
bazel test //...
```

TOOD: move the test under distribtests. Currently it piggybacks on python bazel tests:
https://github.com/grpc/grpc/blob/d18b52f5db44b1bfae42a08b3622a0d9b3688fa2/tools/internal_ci/linux/grpc_python_bazel_test_in_docker.sh#L35
