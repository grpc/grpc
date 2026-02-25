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
import json
import re
import sys
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
    bazel_output = sys.stdin.read()
    result = parse_http_archives(bazel_output)
    json_obj = {"http_archives": result}
    print(json.dumps(json_obj, indent=4))
