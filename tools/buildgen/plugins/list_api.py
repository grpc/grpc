#!/usr/bin/env python3

# Copyright 2016 gRPC authors.
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

import collections
import fnmatch
import os
import re
import sys

import yaml

_RE_API = r"(?:GPRAPI|GRPCAPI|CENSUSAPI)([^#;]*);"


def list_c_apis(filenames):
    for filename in filenames:
        with open(filename, "r") as f:
            text = f.read()
        for m in re.finditer(_RE_API, text):
            api_declaration = re.sub("[ \r\n\t]+", " ", m.group(1))
            type_and_name, args_and_close = api_declaration.split("(", 1)
            args = args_and_close[: args_and_close.rfind(")")].strip()
            last_space = type_and_name.rfind(" ")
            last_star = type_and_name.rfind("*")
            type_end = max(last_space, last_star)
            return_type = type_and_name[0 : type_end + 1].strip()
            name = type_and_name[type_end + 1 :].strip()
            yield {
                "return_type": return_type,
                "name": name,
                "arguments": args,
                "header": filename,
            }


def headers_under(directory):
    for root, dirnames, filenames in os.walk(directory):
        for filename in fnmatch.filter(filenames, "*.h"):
            yield os.path.join(root, filename)


def mako_plugin(dictionary):
    apis = []
    headers = []

    for lib in dictionary["libs"]:
        if lib["name"] in ["grpc", "gpr"]:
            headers.extend(lib["public_headers"])

    apis.extend(list_c_apis(sorted(set(headers))))
    dictionary["c_apis"] = apis


if __name__ == "__main__":
    print(
        (yaml.dump([api for api in list_c_apis(headers_under("include/grpc"))]))
    )
