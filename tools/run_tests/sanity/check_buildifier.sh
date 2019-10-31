#! /bin/bash -ex

GIT_ROOT="$(dirname "$0")/../../.."
TMP_ROOT="/tmp/buildifier_grpc"
git clone -- "$GIT_ROOT" "$TMP_ROOT"
buildifier -r -v -mode=diff $TMP_ROOT
