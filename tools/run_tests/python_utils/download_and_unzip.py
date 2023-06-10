# Copyright 2020 The gRPC Authors
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
"""Download and unzip the target file to the destination."""

from __future__ import print_function

import os
import sys
import tempfile
import zipfile

import requests


def main():
    if len(sys.argv) != 3:
        print("Usage: python download_and_unzip.py [zipfile-url] [destination]")
        sys.exit(1)
    download_url = sys.argv[1]
    destination = sys.argv[2]

    with tempfile.TemporaryFile() as tmp_file:
        r = requests.get(download_url)
        if r.status_code != requests.codes.ok:
            print(
                'Download %s failed with [%d] "%s"'
                % (download_url, r.status_code, r.text())
            )
            sys.exit(1)
        else:
            tmp_file.write(r.content)
            print("Successfully downloaded from %s", download_url)
        with zipfile.ZipFile(tmp_file, "r") as target_zip_file:
            target_zip_file.extractall(destination)
        print("Successfully unzip to %s" % destination)


if __name__ == "__main__":
    main()
