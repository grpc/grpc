package(default_visibility=["//visibility:public"])

# To build Python C/C++ extension on Windows, we need to link to python import library pythonXY.lib
# See https://docs.python.org/3/extending/windows.html
cc_import(
    name="%{VARIETY_NAME}_lib",
    interface_library=select({
        "//:windows": ":%{VARIETY_NAME}_import_lib",
        # A placeholder for Unix platforms which makes --no_build happy.
        "//conditions:default": "not-existing.lib",
    }),
    system_provided=1,
)

cc_library(
    name="%{VARIETY_NAME}_headers",
    hdrs=[":%{VARIETY_NAME}_include"],
    deps=select({
        "//:windows": [":%{VARIETY_NAME}_lib"],
        "//conditions:default": [],
    }),
    includes=["%{VARIETY_NAME}_include"],
)

%{PYTHON_INCLUDE_GENRULE}
%{PYTHON_IMPORT_LIB_GENRULE}
