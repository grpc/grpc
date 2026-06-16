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
import logging
import json
import re
import sys

_MIRROR_SITE_PATTERN = "https://storage.googleapis.com/grpc-bazel-mirror"
_SUPPORTED_RULE_KINDS = ["http_archive", "http_file", "python_repository", "whl_library"]
_LOGGED_FIELDS = ["repoRuleName", "name", "canonicalName", "apparentName"]
logger = logging.getLogger(__name__)

def parse_ndjson(file_path):
    repos = []
    with open(file_path, "r", encoding="utf-8") as f:
        for line in f:
            obj = json.loads(line)
            repos.append(obj)
    return repos

def main():
    logging.basicConfig(level=logging.INFO)
    if len(sys.argv) < 3:
        logger.error("Usage: update_mirror_helper.py <input> <output>")
        sys.exit(1)
    input_path = sys.argv[1]
    output_path = sys.argv[2]
    repos = parse_ndjson(input_path)
    output_lines = []
    for repo in repos:
        if repo["repoRuleName"] not in _SUPPORTED_RULE_KINDS:
            continue
        urls = []
        for attr in repo["attribute"]:
            if attr["name"] == "url" and attr["stringValue"]:
                urls.append(attr["stringValue"])
            elif attr["name"] == "urls":
                urls.extend(attr.get("stringListValue", list()))

        logging_data = {k: repo[k] for k in _LOGGED_FIELDS if k in repo}
        logging_data["urls"] = urls
        logger.debug(f"Processing repo definition: {json.dumps(logging_data, indent=4)}")
        for url in urls:
            if _MIRROR_SITE_PATTERN not in url:
                output_lines.append(f"{url.removeprefix('https://')}")
    output = "\n".join(output_lines)
    with open(output_path, "w") as file:
        contents = file.write(output)

if __name__ == "__main__":
  main()
