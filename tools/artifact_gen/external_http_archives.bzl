load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

## @envoy_api:
# <builtin>
http_archive(
    name = "envoy_api+",
    urls = ["https://github.com/envoyproxy/data-plane-api/archive/4de3c74cf21a9958c1cf26d8993c55c6e0d28b49.tar.gz"],
    integrity = "sha256-zYtJYUQItDvUXZDj6Y1p4k7qYy/0KsO/uLymi8MeN38=",
    strip_prefix = "data-plane-api-4de3c74cf21a9958c1cf26d8993c55c6e0d28b49",
    remote_file_urls = {},
    remote_file_integrity = {},
    remote_patches = {"https://bcr.bazel.build/modules/envoy_api/0.0.0-20250128-4de3c74/patches/bzlmod_fixes.patch": "sha256-zfAI99yHPb5Sj0rkqzgmezf5N1LFH2QIDK3dG+jeu6Q=", "https://bcr.bazel.build/modules/envoy_api/0.0.0-20250128-4de3c74/patches/module_dot_bazel.patch": "sha256-crm9cbHr3s+t4IlJnGU+lWMZUZ/4QoNV6rI/o52vQ80="},
    remote_patch_strip = 1,
)
# Rule envoy_api+ instantiated at (most recent call last):
#   <builtin> in <toplevel>
# Rule http_archive defined at (most recent call last):
#   /usr/local/google/home/eostroukhov/.cache/bazel/_bazel_eostroukhov/b8b1883bb9aee6be853e56a4a700c6c1/external/bazel_tools/tools/build_defs/repo/http.bzl:392:31 in <toplevel>

## @com_google_googleapis:
# <builtin>
http_archive(
    name = "googleapis+",
    urls = ["https://github.com/googleapis/googleapis/archive/280725e991516d4a0f136268faf5aa6d32d21b54.tar.gz"],
    integrity = "sha256-WpRQzxrRGHyCorXN/1Pk1YS21FKStfcbUECDrLA619A=",
    strip_prefix = "googleapis-280725e991516d4a0f136268faf5aa6d32d21b54",
    patches = ["//bazel:googleapis.modules.patch"],
    patch_strip = 1,
)
# Rule googleapis+ instantiated at (most recent call last):
#   <builtin> in <toplevel>
# Rule http_archive defined at (most recent call last):
#   /usr/local/google/home/eostroukhov/.cache/bazel/_bazel_eostroukhov/b8b1883bb9aee6be853e56a4a700c6c1/external/bazel_tools/tools/build_defs/repo/http.bzl:392:31 in <toplevel>

## @com_github_cncf_xds:
# <builtin>
http_archive(
    name = "xds+",
    urls = ["https://github.com/cncf/xds/archive/555b57ec207be86f811fb0c04752db6f85e3d7e2.tar.gz"],
    integrity = "sha256-DIxPD2f+2We1EEn31eLKepvUM5cKKciOJyyGZTKBcvU=",
    strip_prefix = "xds-555b57ec207be86f811fb0c04752db6f85e3d7e2",
    remote_file_urls = {},
    remote_file_integrity = {},
    remote_patches = {"https://bcr.bazel.build/modules/xds/0.0.0-20240423-555b57e/patches/bzlmod.patch": "sha256-zrpUCLxhXC7WrPx1SB5ZXy1xROlKk8ZOEaYkwCdiZg4="},
    remote_patch_strip = 1,
)
# Rule xds+ instantiated at (most recent call last):
#   <builtin> in <toplevel>
# Rule http_archive defined at (most recent call last):
#   /usr/local/google/home/eostroukhov/.cache/bazel/_bazel_eostroukhov/b8b1883bb9aee6be853e56a4a700c6c1/external/bazel_tools/tools/build_defs/repo/http.bzl:392:31 in <toplevel>

## @com_envoyproxy_protoc_gen_validate:
# <builtin>
http_archive(
    name = "protoc-gen-validate+",
    urls = ["https://github.com/bufbuild/protoc-gen-validate/archive/refs/tags/v1.2.1.tar.gz"],
    integrity = "sha256-5HGDUnVN8Tk7h5K2MTOKqFYvOQ6BYHg+NlRUvBHZYyg=",
    strip_prefix = "protoc-gen-validate-1.2.1",
    remote_file_urls = {"MODULE.bazel": ["https://bcr.bazel.build/modules/protoc-gen-validate/1.2.1.bcr.1/overlay/MODULE.bazel"]},
    remote_file_integrity = {"MODULE.bazel": "sha256-S/CWdrYvpYeuB+BzQgp27Idm3M51ReX4xoz6jkhLUSA="},
    remote_patches = {},
    remote_patch_strip = 1,
)
# Rule protoc-gen-validate+ instantiated at (most recent call last):
#   <builtin> in <toplevel>
# Rule http_archive defined at (most recent call last):
#   /usr/local/google/home/eostroukhov/.cache/bazel/_bazel_eostroukhov/b8b1883bb9aee6be853e56a4a700c6c1/external/bazel_tools/tools/build_defs/repo/http.bzl:392:31 in <toplevel>

## opencensus-proto@0.4.1.bcr.2:
# <builtin>
http_archive(
    name = "opencensus-proto+",
    urls = ["https://github.com/census-instrumentation/opencensus-proto/archive/refs/tags/v0.4.1.tar.gz"],
    integrity = "sha256-49iff57YTJtu7oGMLpMGlQUZQCv4A2mLFcMQt3yi8PM=",
    strip_prefix = "opencensus-proto-0.4.1/src",
    remote_file_urls = {"MODULE.bazel": ["https://bcr.bazel.build/modules/opencensus-proto/0.4.1.bcr.2/overlay/MODULE.bazel"]},
    remote_file_integrity = {"MODULE.bazel": "sha256-eJcGpxSFX5LFyM/PHvMru2Tc07fJBmdWrXmG7FlwnSk="},
    remote_patches = {"https://bcr.bazel.build/modules/opencensus-proto/0.4.1.bcr.2/patches/py-proto-library.patch": "sha256-CZCdIqmnl+ZncUmGl59JVBwUSdxMmJnrQ/ZxBR544yU="},
    remote_patch_strip = 2,
)
# Rule opencensus-proto+ instantiated at (most recent call last):
#   <builtin> in <toplevel>
# Rule http_archive defined at (most recent call last):
#   /usr/local/google/home/eostroukhov/.cache/bazel/_bazel_eostroukhov/b8b1883bb9aee6be853e56a4a700c6c1/external/bazel_tools/tools/build_defs/repo/http.bzl:392:31 in <toplevel>
