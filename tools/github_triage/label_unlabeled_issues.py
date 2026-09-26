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

"""Checks open issues from the last N days and assigns 'unassigned' if unlabeled."""

import datetime
import os
from typing import Sequence

from absl import app
from absl import flags
import github


_DAYS = flags.DEFINE_integer(
    "days",
    1,
    "Number of past days to check for unlabeled issues.",
)
_LABEL = flags.DEFINE_string(
    "label",
    "unassigned",
    "Label to assign to issues that have no labels.",
)
_DRY_RUN = flags.DEFINE_boolean(
    "dry-run",
    False,
    "Runs the script in dry-run mode without modifying issues.",
)


def main(argv: Sequence[str]) -> None:
  if len(argv) > 1:
    raise app.UsageError("Too many command-line arguments.")

  # Change working directory to the same as that of the script
  current_path = os.path.abspath(__file__)
  dname = os.path.dirname(current_path)
  os.chdir(dname)

  g = github.Github(os.environ["GITHUB_TOKEN"])
  repo = g.get_repo("grpc/grpc")

  cutoff_date = datetime.datetime.now(
      datetime.timezone.utc
  ) - datetime.timedelta(days=_DAYS.value)

  print(
      f"Checking open issues created since {cutoff_date.isoformat()} "
      f"(last {_DAYS.value} days)..."
  )

  # Passing `since=cutoff_date` limits API pagination to issues updated since
  # `cutoff_date` (which includes all issues created since `cutoff_date`).
  for issue in repo.get_issues(state="open", since=cutoff_date):
    # Skip pull requests (GitHub's issues API returns both issues and PRs).
    if issue.pull_request is not None:
      continue

    created_at = issue.created_at
    if created_at.tzinfo is None:
      created_at = created_at.replace(tzinfo=datetime.timezone.utc)

    if created_at < cutoff_date:
      continue

    if not issue.labels:
      print(
          f"Issue #{issue.number} ({issue.title!r}, created {created_at}) "
          f"has no labels. Adding label '{_LABEL.value}'."
      )
      if _DRY_RUN.value:
        print(f"  DRY RUN: issue.add_to_labels('{_LABEL.value}')")
      else:
        issue.add_to_labels(_LABEL.value)


if __name__ == "__main__":
  app.run(main)
