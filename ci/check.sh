#!/usr/bin/env bash

set -e

bazel test --config=ci //...

rm -rf go/udpa

tools/generate_go_protobuf.py

git add go/udpa

echo "If this check fails, apply following diff:"
git diff HEAD
git diff HEAD --quiet
