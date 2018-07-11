load("@grpc_python_dependencies//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

GRPCIO_TESTS_UNIT = [
    "_api_test.py",
    "_auth_context_test.py",
    "_auth_test.py",
    "_channel_args_test.py",
    "_channel_close_test.py",
    "_channel_connectivity_test.py",
    "_channel_ready_future_test.py",
    "_compression_test.py",
    "_credentials_test.py",
    "_empty_message_test.py",
    "_exit_test.py",
    "_interceptor_test.py",
    "_invalid_metadata_test.py",
    "_invocation_defects_test.py",
    "_metadata_code_details_test.py",
    "_metadata_test.py",
    "_reconnect_test.py",
    "_resource_exhausted_test.py",
    "_rpc_test.py",
    "_server_ssl_cert_config_test.py",
    "_server_test.py",
    "_session_cache_test.py",
]

py_library(
    name = "resources",
    srcs = ["resources.py"],
    data=[
        "//src/python/grpcio_tests/tests/unit/credentials",
    ],
)

py_library(
    name = "test_common",
    srcs = ["test_common.py"],
)

py_library(
    name = "_exit_scenarios",
    srcs = ["_exit_scenarios.py"],
)

py_library(
    name = "_thread_pool",
    srcs = ["_thread_pool.py"],
)

py_library(
    name = "_from_grpc_import_star",
    srcs = ["_from_grpc_import_star.py"],
)

[
    py_test(
        name=test_file_name[:-3],
        size="small",
        timeout="moderate",
        srcs=[test_file_name],
        main=test_file_name,
        deps=[
            "//src/python/grpcio/grpc:grpcio",
            ":resources",
            ":test_common",
            ":_exit_scenarios",
            ":_thread_pool",
            ":_from_grpc_import_star",
            "//src/python/grpcio_tests/tests/unit/framework/common",
            "//src/python/grpcio_tests/tests/testing",
            requirement('six'),
        ],
        imports=["../../",],
        data=[
            "//src/python/grpcio_tests/tests/unit/credentials",
        ],
    ) for test_file_name in GRPCIO_TESTS_UNIT
]


