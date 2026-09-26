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

import os
from os import path
from typing import Sequence

from absl import app
from github import Github
import requests
from requests.auth import HTTPBasicAuth

assignee_rotation_list = [
    "markdroth",
    "pawbhard",
    "rishesh007",
    "murgatroid99",
    "yuanweiz",
    "aadikapur",
    "stunner-07",
]


def main(argv: Sequence[str]) -> None:
  if len(argv) > 1:
    raise app.UsageError("Too many command-line arguments.")

  # Change working directory to the same as that of the script
  current_path = os.path.abspath(__file__)
  dname = os.path.dirname(current_path)
  os.chdir(dname)

  # Get next assignee in the rotation.
  # Note: The value of last_assignee_index checked into git (0) is only the
  # initial seed. During scheduled runs of .github/workflows/issue-triage.yaml,
  # last_assignee_index is restored at the start of the job via
  # actions/cache/restore and saved back to the GitHub Actions cache at the end
  # of the job via actions/cache/save so the rotation continues across runs
  # without committing back to master.
  f = open("last_assignee_index", "r")
  index_str = f.read().strip()
  f.close()

  # Get list of known googlers
  f = open("known_googlers", "r")
  known_googlers = f.read().strip().split()

  if index_str == "":
    index_num = 0
  else:
    index_num = int(index_str)

  g = Github(os.environ["GITHUB_TOKEN"])
  repo = g.get_repo("grpc/grpc")

  # Assign issues
  for issue in repo.get_issues(state="open"):
    if issue.assignee == None:
      lang_labels = [
          label.name for label in issue.labels if "lang" in label.name
      ]
      labels = [label.name for label in issue.labels]
      if (
          ("lang/core" in lang_labels)
          or ("lang/c++" in lang_labels)
          or (len(lang_labels) == 0)
      ):
        if "untriaged" in labels:
          print(issue, issue.labels)
          print("to be assigned to ", assignee_rotation_list[index_num])
          issue.add_to_assignees(assignee_rotation_list[index_num])
          index_num = (index_num + 1) % len(assignee_rotation_list)

  # Assign pull requests
  for pr in repo.get_pulls():
    if pr.assignee != None:
      continue
    if pr.user.login in known_googlers:
      continue
    lang_labels = [label.name for label in pr.labels if "lang" in label.name]
    if (
        ("lang/core" in lang_labels)
        or ("lang/c++" in lang_labels)
        or (len(lang_labels) == 0)
    ):
      print(pr, pr.user.login, pr.labels)
      print("to be assigned to ", assignee_rotation_list[index_num])
      pr.add_to_assignees(assignee_rotation_list[index_num])
      index_num = (index_num + 1) % len(assignee_rotation_list)

  # Save the next assignee in the rotation
  f = open("last_assignee_index", "w")
  f.write(str(index_num))
  f.close()


if __name__ == "__main__":
  app.run(main)
