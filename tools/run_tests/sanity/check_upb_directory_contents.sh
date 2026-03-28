#!/bin/sh
set -e

export TEST=true

dir=$(dirname "$0")
grpc_root=$(realpath ${dir}/../../..)
cd ${grpc_root}

diff_after_applying_patch=$(mktemp /tmp/upbXXXXXX)

# Apply our patch.
pushd third_party/protobuf
git apply "${grpc_root}/third_party/upb.patch"
popd

cleanup() {
  echo "restoring third_party/protobuf"
  cd ${grpc_root}/third_party/protobuf
  git reset --hard
  rm ${diff_after_applying_patch}
}

trap cleanup EXIT


if ! (git diff --no-index "third_party/protobuf/upb" "third_party/upb/upb" > $diff_after_applying_patch) ; then
  echo "third_party/upb has unexpected change. Diff contents:"
  cat ${diff_after_applying_patch}
  exit 1
fi
