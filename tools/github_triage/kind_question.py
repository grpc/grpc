#!/usr/bin/env python3

# Copyright 2026 gRPC authors.
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

# This script redirects questions from github.com/grpc/grpc/issues to other more appropriate forums like stack-overflow or the grpc.io mailing list.

import datetime
import os
from os import path
from typing import Sequence

from absl import app
from github import Auth
from github import Github
import requests
from requests.auth import HTTPBasicAuth


def main(argv: Sequence[str]) -> None:
    if len(argv) > 1:
        raise app.UsageError("Too many command-line arguments.")

    # Change working directory to the same as that of the script
    current_path = os.path.abspath(__file__)
    dname = os.path.dirname(current_path)
    os.chdir(dname)

    g = Github(auth=Auth.Token(os.environ["GITHUB_TOKEN"]), per_page=200)
    repo = g.get_repo("grpc/grpc")

    # Assign issues
    for issue in repo.get_issues(state="open"):
        labels = [label.name for label in issue.labels]
        if "kind/question" in labels:
            print(issue, issue.labels)
            comment = """PLEASE DO NOT POST A QUESTION HERE.
This form is for bug reports and feature requests ONLY!
For general questions and troubleshooting, please ask/look for answers at StackOverflow, with "grpc" tag: https://stackoverflow.com/questions/tagged/grpc
For questions that specifically need to be answered by gRPC team members, please ask/look for answers at grpc.io mailing list: https://groups.google.com/forum/#!forum/grpc-io"""
            print(comment)
            issue.create_comment(comment)
            issue.edit(state="closed")


if __name__ == "__main__":
    app.run(main)
