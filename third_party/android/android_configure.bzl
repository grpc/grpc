"""Repository rule for Android SDK and NDK autoconfiguration.

This rule is a no-op unless the required android environment variables are set.
"""

# Based on https://github.com/tensorflow/tensorflow/tree/34c03ed67692eb76cb3399cebca50ea8bcde064c/third_party/android
# Workaround for https://github.com/bazelbuild/bazel/issues/14260

_ANDROID_NDK_HOME = "ANDROID_NDK_HOME"
_ANDROID_SDK_HOME = "ANDROID_HOME"

def _escape_for_windows(path):
    """Properly escape backslashes for Windows.

    Ideally, we would do this conditionally, but there is seemingly no way to
    determine whether or not this is being called from Windows.
    """
    return path.replace("\\", "\\\\")

def _android_autoconf_impl(repository_ctx):
    sdk_home = repository_ctx.os.environ.get(_ANDROID_SDK_HOME)
    ndk_home = repository_ctx.os.environ.get(_ANDROID_NDK_HOME)

    # version 31.0.0 won't work https://stackoverflow.com/a/68036845
    sdk_rule = ""
    if sdk_home:
        sdk_rule = """
    native.android_sdk_repository(
        name="androidsdk",
        path="{}",
        build_tools_version="30.0.3",
    )
""".format(_escape_for_windows(sdk_home))

    # Note that Bazel does not support NDK 22 yet, and Bazel 3.7.1 only
    # supports up to API level 29 for NDK 21
    ndk_rule = ""
    if ndk_home:
        ndk_rule = """
    native.android_ndk_repository(
        name="androidndk",
        path="{}",
    )
""".format(_escape_for_windows(ndk_home))

    if ndk_rule == "" and sdk_rule == "":
        sdk_rule = "pass"

    repository_ctx.file("BUILD.bazel", "")
    repository_ctx.file("android_configure.bzl", """
def android_workspace():
    {}
    {}
    """.format(sdk_rule, ndk_rule))

android_configure = repository_rule(
    implementation = _android_autoconf_impl,
    environ = [
        _ANDROID_NDK_HOME,
        _ANDROID_SDK_HOME,
    ],
)
