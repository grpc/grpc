#!/bin/bash

# This script updates checked-in generated files (currently CMakeLists.txt,
# descriptor.upb.h, and descriptor.upb.c), commits the resulting change, and
# pushes it. This does not do anything useful when run manually, but should be
# run by our GitHub action instead.

set -ex

# Exit early if the previous commit was made by the bot. This reduces the risk
# of a bug causing an infinite loop of auto-generated commits.
if (git log -1 --pretty=format:'%an' | grep -q "Protobuf Team Bot"); then
  echo "Previous commit was authored by bot"
  exit 0
fi

cd $(dirname -- "$0")/..
bazel test //cmake:test_generated_files || bazel-bin/cmake/test_generated_files --fix

# Try to determine the most recent pull request number.
title=$(git log -1 --pretty='%s')
pr_from_merge=$(echo "$title" | sed -n 's/^Merge pull request #\([0-9]\+\).*/\1/p')
pr_from_squash=$(echo "$title" | sed -n 's/^.*(#\([0-9]\+\))$/\1/p')

pr=""
if [ ! -z "$pr_from_merge" ]; then
  pr="$pr_from_merge"
elif [ ! -z "$pr_from_squash" ]; then
  pr="$pr_from_squash"
fi

if [ ! -z "$pr" ]; then
  commit_message="Auto-generate CMake file lists after PR #$pr"
else
  # If we are unable to determine the pull request number, we fall back on this
  # default commit message. Typically this should not occur, but could happen
  # if a pull request was merged via a rebase.
  commit_message="Auto-generate CMake file lists"
fi

git add -A
git diff --staged --quiet || git commit -am "$commit_message"
git push
