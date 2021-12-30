# Adapted with modifications from tensorflow/third_party/py/

package(default_visibility=["//visibility:public"])

config_setting(
    name="windows",
    values={"cpu": "x64_windows"},
    visibility=["//visibility:public"],
)

config_setting(
    name="python3",
    flag_values = {"@rules_python//python:python_version": "PY3"}
)

cc_library(
    name = "python_lib",
    deps = select({
        ":python3": ["//_python3:_python3_lib"],
        "//conditions:default": ["not-existing.lib"],
    })
)

cc_library(
    name = "python_headers",
    deps = select({
        ":python3": ["//_python3:_python3_headers"],
        "//conditions:default": ["not-existing.headers"],
    })
)
