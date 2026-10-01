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

"""Manages 'disposition/requires reporter action' labels."""

import datetime
import os
from typing import Sequence

from absl import app
from absl import flags
import github

_LABEL = "disposition/requires reporter action"
_EXPIRE_AFTER_DAYS = 30

_DRY_RUN = flags.DEFINE_boolean("dry-run", False, "Runs the script in dry-run.")


def process_labeled_issue(issue, labels):
    """Processes an issue that already has 'disposition/requires reporter action'.

    Args:
      issue: github.issue object
    """
    print(issue, issue.labels)
    # Remove "disposition/requires reporter action" if it is not the last event in
    # the issue
    for event in issue.get_timeline().reversed:
        print(issue.number, event.event, event.created_at)
        if (
            event.event in ("labeled", "unlabeled")
            and event.raw_data["label"]["name"] != _LABEL
        ):
            # Ignore all other labeling changes.
            print(issue.number, "ignoring")
            continue
        if (
            event.event == "labeled"
            and event.raw_data["label"]["name"] == _LABEL
        ):
            if (
                datetime.datetime.now(datetime.timezone.utc) - event.created_at
                > datetime.timedelta(days=_EXPIRE_AFTER_DAYS)
                and "disposition/never stale" not in labels
            ):
                # Close the issue if 30 days have passed since the label was added.
                comment = (
                    'More than 30 days have passed since label "disposition/requires'
                    ' reporter action" was added. Closing this issue. Please feel'
                    " free to re-open/create a new issue if this is still relevant."
                )
                print(f"  Closing Issue #{issue.number}")
                if _DRY_RUN.value:
                    print(
                        "  DRY RUN: \n"
                        "    issue.create_comment(comment)\n"
                        "    issue.edit(state='closed')"
                    )
                    return
                issue.create_comment(comment)
                issue.edit(state="closed")
            return
        # Otherwise, some event occurred that should cause the label to be removed.
        print(
            'Label "disposition/requires reporter action" was not added last.'
            " Removing label."
        )
        print(f"  Removing label from #{issue.number}")
        if _DRY_RUN.value:
            print(f"  DRY RUN: \n    issue.remove_from_labels({_LABEL})")
            return
        issue.remove_from_labels(_LABEL)
        return


def main(argv: Sequence[str]) -> None:
    """Manages 'disposition/requires reporter action' labels."""
    if len(argv) > 1:
        raise app.UsageError("Too many command-line arguments.")

    # Change working directory to the same as that of the script
    current_path = os.path.abspath(__file__)
    dname = os.path.dirname(current_path)
    os.chdir(dname)

    g = github.Github(
        auth=github.Auth.Token(os.environ["GITHUB_TOKEN"]), per_page=200
    )
    repo = g.get_repo("grpc/grpc")

    # Handle issues and pull requests.
    # Note that get_issues gets you both issues and pull requests.
    for issue in repo.get_issues(state="open"):
        labels = [label.name for label in issue.labels]
        if _LABEL in labels:
            process_labeled_issue(issue, labels)


if __name__ == "__main__":
    app.run(main)
