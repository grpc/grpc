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

display_usage () {
  cat << EOF >/dev/stderr
USAGE: $0 PR_ID GITHUB_USER BACKPORT_BRANCHES REVIEWERS [-c PER_BACKPORT_COMMAND]
   PR_ID: The ID of the PR to be backported.
   GITHUB_USER: Your GitHub username.
   BACKPORT_BRANCHES: A space-separated list of branches to which the source PR will be backported.
   REVIEWERS: A comma-separated list of users to add as both reviewer and assignee.
   PER_BACKPORT_COMMAND : An optional command to run after cherrypicking the PR to the target branch.
     If you use this option, ensure your working directory is clean, as "git add -A" will be used to
     incorporate any generated files. Try running "git clean -xdff" beforehand.

Example: $0 25456 gnossen "v1.30.x v1.31.x v1.32.x v1.33.x v1.34.x v1.35.x v1.36.x" "menghanl,gnossen"
Example: $0 25493 gnossen "\$(seq 30 33 | xargs -n1 printf 'v1.%s.x ')" "menghanl" -c ./tools/dockerfile/push_testing_images.sh
EOF
  exit 1
}

ensure_command "curl"
ensure_command "egrep"
ensure_command "hub"
ensure_command "jq"

if [ "$#" -lt "4" ]; then
  display_usage
fi

PR_ID="$1"
GITHUB_USER="$2"
BACKPORT_BRANCHES="$3"
REVIEWERS="$4"
shift 4

PER_BACKPORT_COMMAND=""
while getopts "c:" OPT; do
  case "$OPT" in
    c )
      PER_BACKPORT_COMMAND="$OPTARG"
      ;;
    \? )
      echo "Invalid option: $OPTARG" >/dev/stderr
      display_usage
      ;;
    : )
      echo "Invalid option: $OPTARG requires an argument." >/dev/stderr
      display_usage
      ;;
  esac
done

if [[ ! -z "$(git status --porcelain)" && ! -z "$PER_BACKPORT_COMMAND" ]]; then
  echo "Your working directory is not clean. Try running `git clean -xdff`. Warning: This is irreversible." > /dev/stderr
  exit 1
fi

if [ -z "$GITHUB_TOKEN" ]; then
  echo "A GitHub token is required to run this script. See " \
         "https://docs.github.com/en/github/authenticating-to-github/creating-a-personal-access-token" \
         " for more information" >/dev/stderr
  exit 1
fi

echo "This script will create a collection of backport PRs. You will probably " \
       "have to touch your gnubby a frustrating number of times. C'est la vie."
printf "Press any key to continue."
read -r RESPONSE </dev/tty
printf "\n"


PR_DATA=$(curl -s -u "$GITHUB_USER:$GITHUB_TOKEN" \
          -H "Accept: application/vnd.github.v3+json" \
          "https://api.github.com/repos/grpc/grpc/pulls/$PR_ID")

STATE=$(echo "$PR_DATA" | jq -r '.state')
if [ "$STATE" != "open" ]; then
  TARGET_COMMITS=$(echo "$PR_DATA" | jq -r '.merge_commit_sha')
  FETCH_HEAD_REF=$(echo "$PR_DATA" | jq -r '.base.ref')
  SOURCE_REPO=$(echo "$PR_DATA" | jq -r '.base.repo.full_name')
else
  COMMITS_URL=$(echo "$PR_DATA" | jq -r '.commits_url')
  COMMITS_DATA=$(curl -s -u "$GITHUB_USER:$GITHUB_TOKEN" \
                 -H "Accept: application/vnd.github.v3+json" \
                 "$COMMITS_URL")
  TARGET_COMMITS=$(echo "$COMMITS_DATA" | jq -r '. | map(.sha) | join(" ")')
  FETCH_HEAD_REF=$(echo "$PR_DATA" | jq -r '.head.sha')
  SOURCE_REPO=$(echo "$PR_DATA" | jq -r '.head.repo.full_name')
fi
PR_TITLE=$(echo "$PR_DATA" | jq -r '.title')
PR_DESCRIPTION=$(echo "$PR_DATA" | jq -r '.body')
LABELS=$(echo "$PR_DATA" | jq -r '.labels | map(.name) | join(",")')

set -x

git fetch "git@github.com:$SOURCE_REPO.git" "$FETCH_HEAD_REF"

BACKPORT_PRS=""
for BACKPORT_BRANCH in $BACKPORT_BRANCHES; do
  echo "Backporting $TARGET_COMMITS to $BACKPORT_BRANCH."

  git checkout "origin/$BACKPORT_BRANCH"

  BRANCH_NAME="backport_${PR_ID}_to_${BACKPORT_BRANCH}"

  # To make the script idempotent.
  git branch -D "$BRANCH_NAME" || true
  git checkout "$BACKPORT_BRANCH"
  git checkout -b "$BRANCH_NAME"

  for TARGET_COMMIT in $TARGET_COMMITS; do
    git cherry-pick -m 1 "$TARGET_COMMIT"
  done

  if [[ ! -z "$PER_BACKPORT_COMMAND" ]]; then
    git submodule update --init --recursive

    # To remove dangling submodules.
    git clean -xdff
    eval "$PER_BACKPORT_COMMAND"
    git add -A
    git commit --amend --no-edit
  fi

  BACKPORT_PR=$(hub pull-request -p -m "[Backport] $PR_TITLE" \
                  -m "*Beep boop. This is an automatically generated backport of #${PR_ID}.*" \
                  -m "$PR_DESCRIPTION" \
                  -l "$LABELS" \
                  -b "$GITHUB_USER:$BACKPORT_BRANCH" \
                  -r "$REVIEWERS" \
                  -a "$REVIEWERS" | tail -n 1)
  BACKPORT_PRS+="$BACKPORT_PR\n"

  # TODO: Turn on automerge once the Github API allows it.
done

printf "Your backport PRs have been created:\n$BACKPORT_PRS"
