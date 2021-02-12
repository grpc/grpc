# Any external dependency imported in the api/ .protos requires entries in
# the maps below, to allow the Bazel proto and language specific bindings to be
# inferred from the import directives.
#
# This file needs to be interpreted as both Python 3 and Starlark, so only the
# common subset of Python should be used.

# This maps from .proto import directive path to the Bazel dependency path for
# external dependencies. Since BUILD files are generated, this is the canonical
# place to define this mapping.
EXTERNAL_PROTO_IMPORT_BAZEL_DEP_MAP = {
    "google/api/expr/v1alpha1/checked.proto": "@com_google_googleapis//google/api/expr/v1alpha1:checked_proto",
    "google/api/expr/v1alpha1/syntax.proto": "@com_google_googleapis//google/api/expr/v1alpha1:syntax_proto",
    "metrics.proto": "@prometheus_metrics_model//:client_model",
    "opencensus/proto/trace/v1/trace.proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_proto",
    "opencensus/proto/trace/v1/trace_config.proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_config_proto",
}

# This maps from the Bazel proto_library target to the Go language binding target for external dependencies.
EXTERNAL_PROTO_GO_BAZEL_DEP_MAP = {
    "@com_google_googleapis//google/api/expr/v1alpha1:checked_proto": "@com_google_googleapis//google/api/expr/v1alpha1:expr_go_proto",
    "@com_google_googleapis//google/api/expr/v1alpha1:syntax_proto": "@com_google_googleapis//google/api/expr/v1alpha1:expr_go_proto",
    "@opencensus_proto//opencensus/proto/trace/v1:trace_proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_proto_go",
    "@opencensus_proto//opencensus/proto/trace/v1:trace_config_proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_and_config_proto_go",
}

# This maps from the Bazel proto_library target to the C++ language binding target for external dependencies.
EXTERNAL_PROTO_CC_BAZEL_DEP_MAP = {
    "@com_google_googleapis//google/api/expr/v1alpha1:checked_proto": "@com_google_googleapis//google/api/expr/v1alpha1:checked_cc_proto",
    "@com_google_googleapis//google/api/expr/v1alpha1:syntax_proto": "@com_google_googleapis//google/api/expr/v1alpha1:syntax_cc_proto",
    "@opencensus_proto//opencensus/proto/trace/v1:trace_proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_proto_cc",
    "@opencensus_proto//opencensus/proto/trace/v1:trace_config_proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_config_proto_cc",
}

# This maps from the Bazel proto_library target to the Python language binding target for external dependencies.
EXTERNAL_PROTO_PY_BAZEL_DEP_MAP = {
    "@com_google_googleapis//google/api/expr/v1alpha1:checked_proto": "@com_google_googleapis//google/api/expr/v1alpha1:checked_py_proto",
    "@com_google_googleapis//google/api/expr/v1alpha1:syntax_proto": "@com_google_googleapis//google/api/expr/v1alpha1:syntax_py_proto",
    "@opencensus_proto//opencensus/proto/trace/v1:trace_proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_proto_py",
    "@opencensus_proto//opencensus/proto/trace/v1:trace_config_proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_config_proto_py",
}
