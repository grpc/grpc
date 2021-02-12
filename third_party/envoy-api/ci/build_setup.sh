#!/bin/bash

# Configure environment variables for Bazel build and test.

set -e

[ -z "${NUM_CPUS}" ] && NUM_CPUS=`grep -c ^processor /proc/cpuinfo`

export ENVOY_SRCDIR=/source

export BUILD_DIR=/build
mkdir -p ${BUILD_DIR}

# Create a fake home. Python site libs tries to do getpwuid(3) if we don't and
# the CI Docker image gets confused as it has no passwd entry when running
# non-root unless we do this.
FAKE_HOME=/tmp/fake_home
mkdir -p "${FAKE_HOME}"
export HOME="${FAKE_HOME}"
export PYTHONUSERBASE="${FAKE_HOME}"

# Environment setup.
export USER=bazel
export TEST_TMPDIR=/build/tmp
export BAZEL="bazel"

# Not sandboxing, since non-privileged Docker can't do nested namespaces.
BAZEL_OPTIONS="--package_path %workspace%:/source"
export BAZEL_QUERY_OPTIONS="${BAZEL_OPTIONS}"
export BAZEL_BUILD_OPTIONS="--strategy=Genrule=standalone --spawn_strategy=standalone \
  --verbose_failures ${BAZEL_OPTIONS} --jobs=${NUM_CPUS} \
  --action_env=HOME --action_env=PYTHONUSERBASE"
export BAZEL_TEST_OPTIONS="${BAZEL_BUILD_OPTIONS} --cache_test_results=no --test_output=all --test_env=HOME --test_env=PYTHONUSERBASE"
[[ "${BAZEL_EXPUNGE}" == "1" ]] && "${BAZEL}" clean --expunge

function cleanup() {
  # Remove build artifacts. This doesn't mess with incremental builds as these
  # are just symlinks.
  rm -f "${ENVOY_SRCDIR}"/bazel-*
}
trap cleanup EXIT
