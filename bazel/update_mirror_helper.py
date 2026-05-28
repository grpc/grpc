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

import base64
import json
import re
import sys

# TODO(weizheyuan): Remove this corner case.
#
# libpfm+ uses sourceforge url which has different scheme than github.
# in short term we can use `gsutil cp` for manual upload.
_EXCLUDE_REPOS = ["libpfm+"]
_MIRROR_SITE_PATTERN = "https://storage.googleapis.com/grpc-bazel-mirror"

def _integrity_to_sha256(integrity: str) -> str:
    """Convert a SRI to sha256 checksum hex string"""
    matches = re.match("sha256-(.*)", integrity)
    if matches is None:
        return None
    sha256_base64 = matches.group(1)
    sha256_bytes = base64.b64decode(sha256_base64)
    return sha256_bytes.hex()

def parse_ndjson(file_path):
    repos = []
    with open(file_path, "r", encoding="utf-8") as f:
        for line_number, line in enumerate(f, 1):
            # Clean up whitespace and skip empty lines
            line = line.strip()
            if not line:
                print(f"line: {line}")
                continue
            try:
                # Parse the individual JSON object
                obj = json.loads(line)
                repos.append(obj)
            except json.JSONDecodeError as e:
                print(f"Skipping invalid JSON on line {line_number}: {e}")
    return repos

def main():
    if len(sys.argv) < 3:
        print("Usage: update_mirror_helper.py <input> <output>")
        sys.exit(1)
    input_path = sys.argv[1]
    output_path = sys.argv[2]
    repos = parse_ndjson(input_path)
    output_lines = []
    for repo in repos:
        if repo["repoRuleName"] != "http_archive":
            continue
        canonical_name = repo["canonicalName"]
        if canonical_name in _EXCLUDE_REPOS:
            continue
        archive = {}
        urls = []
        archive["urls"] = urls
        archive["canonicalName"] = canonical_name
        archive["apparentName"] = repo["apparentName"]
        sha256 = ""
        for attr in repo["attribute"]:
            if attr["name"] == "url" and attr["stringValue"]:
                urls.append(attr["stringValue"])
            elif attr["name"] == "urls":
                urls.extend(attr.get("stringListValue", list()))
            elif attr["name"] == "sha256" and attr["stringValue"]:
                sha256 = attr["stringValue"]
                archive["sha256"] = sha256
            elif attr["name"] == "integrity"and attr["stringValue"]:
                sha256 = _integrity_to_sha256(attr["stringValue"])
                archive["sha256"] = sha256
        print(f"archive={json.dumps(archive, indent=4)}")
        for url in urls:
            if _MIRROR_SITE_PATTERN not in url:
                output_lines.append(f"{url.removeprefix('https://')} {sha256}")
    output = "\n".join(output_lines)
    with open(output_path, "w") as file:
        contents = file.write(output)

if __name__ == "__main__":
  main()
