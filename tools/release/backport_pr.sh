#!/bin/bash
#Copyright 2021 The gRPC authors.
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

set -euo pipefail

ensure_command () {
  if command -v "$1" 1>/dev/null 2>&1; then
    return 0
  else
    echo "$1 is not installed. Please install it to proceed." 1>&2
    exit 1
  fi
}

ensure_command "curl"
ensure_command "egrep"
ensure_command "hub"
ensure_command "jq"

if [ -z "$GITHUB_TOKEN" ]; then
  echo "A GitHub token is required to run this script. See " \
         "https://docs.github.com/en/github/authenticating-to-github/creating-a-personal-access-token" \
         " for more information" >/dev/stderr
  exit 1
fi

if [ "$#" != "4" ]; then
  echo "USAGE: $0 PR_NUMBER GITHUB_USER BACKPORT_BRANCHES REVIEWERS" >/dev/stderr
  echo "   PR_NUMBER: The number for the PR to be backported." >/dev/stderr
  echo "   GITHUB_USER: Your GitHub username." >/dev/stderr
  echo "   BACKPORT_BRANCHES: A space-separated list of branches to which to backport." >/dev/stderr
  echo "   REVIEWERS: A comma-separated list of users add as reviewers and assignees." >/dev/stderr
  echo "" >/dev/stderr
  echo "Example: $0 25456 gnossen \"v1.30.x v1.31.x v1.32.x v1.33.x v1.34.x v1.35.x v1.36.x\" \"menghanl,gnossen\""
  exit 1
fi

echo "This script will create a collection of backport PRs. Make sure the PR to" \
       " backport has already been merged. You will probably " \
       " have to touch your gnubby a frustrating number of times. C'est la vie."
printf "Press any key to continue."
read -r RESPONSE </dev/tty
printf "\n"

PR_NUMBER="$1"
GITHUB_USER="$2"

BACKPORT_BRANCHES="$3"

REVIEWERS="$4"

PR_DATA=$(curl -s -u "$GITHUB_USER:$GITHUB_TOKEN" \
          -H "Accept: application/vnd.github.v3+json" \
          "https://api.github.com/repos/grpc/grpc/pulls/$PR_NUMBER")

MERGE_COMMIT=$(echo "$PR_DATA" | jq -r '.merge_commit_sha')
PR_TITLE=$(echo "$PR_DATA" | jq -r '.title')
PR_DESCRIPTION=$(echo "$PR_DATA" | jq -r '.body')
LABELS=$(echo "$PR_DATA" | jq -r '.labels | map(.name) | join(",")')

set -x

git fetch origin

BACKPORT_PRS=""
for BACKPORT_BRANCH in $BACKPORT_BRANCHES; do
  echo "Backporting $MERGE_COMMIT to $BACKPORT_BRANCH."

  git checkout "origin/$BACKPORT_BRANCH"

  BRANCH_NAME="backport_${PR_NUMBER}_to_${BACKPORT_BRANCH}"

  # To make the script idempotent.
  git branch -D "$BRANCH_NAME" || true
  git checkout "$BACKPORT_BRANCH"
  git checkout -b "$BRANCH_NAME"

  git cherry-pick -m 1 "$MERGE_COMMIT"
  BACKPORT_PR=$(hub pull-request -p -m "[Backport] $PR_TITLE" \
                  -m "*Beep boop. This is an automatically generated backport of #${PR_NUMBER}.*" \
                  -m "$PR_DESCRIPTION" \
                  -l "$LABELS" \
                  -b "$BACKPORT_BRANCH" \
                  -r "$REVIEWERS" \
                  -a "$REVIEWERS" | tail -n 1)
  BACKPORT_PRS+="$BACKPORT_PR\n"

  # TODO: Turn on automerge once the Github API allows it.
done

printf "Your backport PRs have been created:\n$BACKPORT_PRS"
