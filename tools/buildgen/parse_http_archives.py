#!/usr/bin/env python3
# Copyright 2026 The gRPC Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Helper script to generate xml file containing http_archive() metadata.

import ast
import base64
import re
from typing import Any, Dict, Iterable, List, Optional

HttpArchive = Dict[str, Any]
HttpArchives = List[HttpArchive]
RepoMapping = Dict[str, str]

CANONICAL_TO_APPARENT_NAME_MAPPING = {
    "@@cel-spec+": "@dev_cel",
    "@@googleapis+": "@com_google_googleapis",
    "@@xds+": "@com_github_cncf_xds",
    "@@protoc-gen-validate+": "@com_envoyproxy_protoc_gen_validate",
    "@@opencensus-proto+": "opencensus_proto",
    "@@envoy_api+": "@envoy_api",
}

# TODO(weizheyuan): Maybe use a mature library for SRI
# parsing so we can support other digest algorithms.
# Supporting only sha256 is fine for now because our
# cmake counterpart download_archive() doesn't support
# other algorithms anyway.
def _integrity_to_sha256(integrity: str) -> str:
    """Convert a SRI to sha256 checksum hex string"""
    matches = re.match("sha256-(.*)", integrity)
    if matches is None:
        return None
    sha256_base64 = matches.group(1)
    sha256_bytes = base64.b64decode(sha256_base64)
    return sha256_bytes.hex()

class KeywordVisitor(ast.NodeVisitor):
    def __init__(self):
        self.http_archive = dict()

    def visit_keyword(self, node):
        if node.arg == "name":
            # Canonical name
            canonical_repo = node.value.value
            apparent_repo = CANONICAL_TO_APPARENT_NAME_MAPPING["@@" + canonical_repo]
            self.http_archive["canonical_repo"] = canonical_repo
            self.http_archive["name"] = apparent_repo[1:] # strip leading '@'

        elif node.arg == "url":
            self.http_archive["urls"] = [node.value.value]
        elif node.arg == "urls":
            self.http_archive["urls"] = [e.value for e in node.value.elts]
        elif node.arg == "integrity":
            self.http_archive["hash"] = _integrity_to_sha256(node.value.value)
        elif node.arg == "sha256":
            self.http_archive["hash"] = node.value.value
            # consumed by tools/artifact_gen/extract_metadata_from_bazel_xml.cc
            self.http_archive["sha256"] = node.value.value
        elif node.arg == "strip_prefix":
            self.http_archive["strip_prefix"] = node.value.value
        

class ModuleVisitor(ast.NodeVisitor):
    def __init__(self):
        self.http_archives = list()
    
    def visit_Call(self, node):
        sub_visitor = KeywordVisitor()
        sub_visitor.visit(node)
        self.http_archives.append(sub_visitor.http_archive)

class HttpArchiveInfo:
    def __init__(self):
        self.name = None
        self.urls = []
        self.strip_prefix = None
        self.hash = None

def parse_http_archives(bazel_output: str) -> HttpArchives:
    module = ast.parse(bazel_output)
    visitor = ModuleVisitor()
    visitor.visit(module)
    return visitor.http_archives

if __name__ == "__main__":
    bazel_output = '''
## @@envoy_api+:
# <builtin>
http_archive(
  name = "envoy_api+",
  urls = ["https://github.com/envoyproxy/data-plane-api/archive/6ef568cf4a67362849911d1d2a546fd9f35db2ff.tar.gz"],
  integrity = "sha256-7V5sMZ+OvN8kqUkfhmpZm7mjwZO4WalK0TvTH4W0aFU=",
  strip_prefix = "data-plane-api-6ef568cf4a67362849911d1d2a546fd9f35db2ff",
  remote_file_urls = {},
  remote_file_integrity = {},
  remote_patches = {"https://bcr.bazel.build/modules/envoy_api/0.0.0-20251216-6ef568c/patches/module_dot_bazel.patch": "sha256-vOfVbB4MR3G9b7fewOYg9SO1RRzD+yAB3YYAf09gjnw="},
  remote_patch_strip = 1,
)
# Rule envoy_api+ instantiated at (most recent call last):
#   <builtin> in <toplevel>
# Rule http_archive defined at (most recent call last):
#   /usr/local/google/home/weizheyuan/.cache/bazel/_bazel_weizheyuan/dfd835222162c1a78f49d4137eeab90b/external/bazel_tools/tools/build_defs/repo/http.bzl:392:31 in <toplevel>

## @@googleapis+:
# <builtin>
http_archive(
  name = "googleapis+",
  urls = ["https://github.com/googleapis/googleapis/archive/2193a2bfcecb92b92aad7a4d81baa428cafd7dfd.zip"],
  integrity = "sha256-B6b3AM7lynlvUjCHbthFS/HYzECbOz5jRQW8TzKp9ys=",
  strip_prefix = "googleapis-2193a2bfcecb92b92aad7a4d81baa428cafd7dfd",
  patches = ["//bazel:googleapis/patches/fix_google_cloud_cpp.patch"],
  remote_file_urls = {"MODULE.bazel": ["https://bcr.bazel.build/modules/googleapis/0.0.0-20251003-2193a2bf/overlay/MODULE.bazel"], "extensions.bzl": ["https://bcr.bazel.build/modules/googleapis/0.0.0-20251003-2193a2bf/overlay/extensions.bzl"], "tests/bcr/.bazelrc": ["https://bcr.bazel.build/modules/googleapis/0.0.0-20251003-2193a2bf/overlay/tests/bcr/.bazelrc"], "tests/bcr/BUILD.bazel": ["https://bcr.bazel.build/modules/googleapis/0.0.0-20251003-2193a2bf/overlay/tests/bcr/BUILD.bazel"], "tests/bcr/MODULE.bazel": ["https://bcr.bazel.build/modules/googleapis/0.0.0-20251003-2193a2bf/overlay/tests/bcr/MODULE.bazel"], "tests/bcr/failure_test.bzl": ["https://bcr.bazel.build/modules/googleapis/0.0.0-20251003-2193a2bf/overlay/tests/bcr/failure_test.bzl"]},
  remote_file_integrity = {"MODULE.bazel": "sha256-zJ5e0pTtnr9Czbvd3S3ykEhRnjeXAE3x4/Np8x/08tQ=", "extensions.bzl": "sha256-59FkoqrMX/a/7t3zkSLDmMkuGnajSeOoZI4FZFIwEuw=", "tests/bcr/.bazelrc": "sha256-hFZT+gits3VtXcUkKuh3NCEC9FgBNGuNxaOVCuOiFPk=", "tests/bcr/BUILD.bazel": "sha256-KZzDURLUNDOiMsurUHrnuWYogHPFs7W+Ar9ZrilPLuE=", "tests/bcr/MODULE.bazel": "sha256-4zmBG1uAn7W3ExjUMQChBcrJVSNCE6K8PhIs9SfKy1U=", "tests/bcr/failure_test.bzl": "sha256-AJLzf7NfQOLpWbNIR3TIU28tQMSjd3svlAgh4yCoXm8="},
  remote_patches = {"https://bcr.bazel.build/modules/googleapis/0.0.0-20251003-2193a2bf/patches/module_dot_bazel.patch": "sha256-0u7LsgwaVMjugknE4OKgNG7e/Sn8S7k7stTW6PE1/so=", "https://bcr.bazel.build/modules/googleapis/0.0.0-20251003-2193a2bf/patches/remove_upb_c_rules.patch": "sha256-MmXB+YzXhG05hDbVgw5S/VZRgu4b2qjQl0OHVEEtP7Y="},
  remote_patch_strip = 0,
  patch_args = ["-p1"],
  patch_cmds = [],
)
# Rule googleapis+ instantiated at (most recent call last):
#   <builtin> in <toplevel>
# Rule http_archive defined at (most recent call last):
#   /usr/local/google/home/weizheyuan/.cache/bazel/_bazel_weizheyuan/dfd835222162c1a78f49d4137eeab90b/external/bazel_tools/tools/build_defs/repo/http.bzl:392:31 in <toplevel>

## @@xds+:
# <builtin>
http_archive(
  name = "xds+",
  urls = ["https://github.com/cncf/xds/archive/ee656c7534f5d7dc23d44dd611689568f72017a6.tar.gz"],
  integrity = "sha256-SVNfPDNwAEMJ2lAZTAm7/FKNRwJCTdRufVaieKPfwV0=",
  strip_prefix = "xds-ee656c7534f5d7dc23d44dd611689568f72017a6",
  remote_file_urls = {},
  remote_file_integrity = {},
  remote_patches = {"https://bcr.bazel.build/modules/xds/0.0.0-20251210-ee656c7/patches/bzlmod.patch": "sha256-2kf90WvQ1u/lYxrG4Zwx7OPA6ScyrsFqdKERBlI6yh8="},
  remote_patch_strip = 1,
)
# Rule xds+ instantiated at (most recent call last):
#   <builtin> in <toplevel>
# Rule http_archive defined at (most recent call last):
#   /usr/local/google/home/weizheyuan/.cache/bazel/_bazel_weizheyuan/dfd835222162c1a78f49d4137eeab90b/external/bazel_tools/tools/build_defs/repo/http.bzl:392:31 in <toplevel>

## @@protoc-gen-validate+:
# <builtin>
http_archive(
  name = "protoc-gen-validate+",
  urls = ["https://github.com/bufbuild/protoc-gen-validate/archive/refs/tags/v1.2.1.tar.gz"],
  integrity = "sha256-5HGDUnVN8Tk7h5K2MTOKqFYvOQ6BYHg+NlRUvBHZYyg=",
  strip_prefix = "protoc-gen-validate-1.2.1",
  remote_file_urls = {"MODULE.bazel": ["https://bcr.bazel.build/modules/protoc-gen-validate/1.2.1.bcr.2/overlay/MODULE.bazel"]},
  remote_file_integrity = {"MODULE.bazel": "sha256-O9SxSo58eNvvlzKA3quqE52x/jUKqS2gNzCjH1kIIGg="},
  remote_patches = {"https://bcr.bazel.build/modules/protoc-gen-validate/1.2.1.bcr.2/patches/bazel_9_fixes.patch": "sha256-bv6l2BHe/pw7Zi5HBwBloPAzIC5YxEuqlW+26Brn3fM="},
  remote_patch_strip = 1,
)
# Rule protoc-gen-validate+ instantiated at (most recent call last):
#   <builtin> in <toplevel>
# Rule http_archive defined at (most recent call last):
#   /usr/local/google/home/weizheyuan/.cache/bazel/_bazel_weizheyuan/dfd835222162c1a78f49d4137eeab90b/external/bazel_tools/tools/build_defs/repo/http.bzl:392:31 in <toplevel>

## @@opencensus-proto+:
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
#   /usr/local/google/home/weizheyuan/.cache/bazel/_bazel_weizheyuan/dfd835222162c1a78f49d4137eeab90b/external/bazel_tools/tools/build_defs/repo/http.bzl:392:31 in <toplevel>

## @@cel-spec+:
# <builtin>
http_archive(
  name = "cel-spec+",
  urls = ["https://github.com/google/cel-spec/archive/refs/tags/v0.25.1.tar.gz"],
  integrity = "sha256-E1g8WjEoYWSESYRbcJciZ2o8m0M5a2uOnL5FOP63StI=",
  strip_prefix = "cel-spec-0.25.1",
  remote_file_urls = {},
  remote_file_integrity = {},
  remote_patches = {"https://bcr.bazel.build/modules/cel-spec/0.25.1/patches/module_dot_bazel.patch": "sha256-6/KX6uCZYufXI/BINlGN3qKtvQbhQPedsBTaaOevNOw="},
  remote_patch_strip = 0,
)
# Rule cel-spec+ instantiated at (most recent call last):
#   <builtin> in <toplevel>
# Rule http_archive defined at (most recent call last):
#   /usr/local/google/home/weizheyuan/.cache/bazel/_bazel_weizheyuan/dfd835222162c1a78f49d4137eeab90b/external/bazel_tools/tools/build_defs/repo/http.bzl:392:31 in <toplevel>

'''
    result = parse_http_archives(bazel_output)
