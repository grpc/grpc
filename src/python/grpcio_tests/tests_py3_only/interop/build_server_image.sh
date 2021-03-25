#!/bin/bash

set -x

VERSION=1

cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.."

# We build outside of the container to take advantage of incremental builds.
tools/bazel build -c dbg //src/python/grpcio_tests/tests_py3_only/interop:xds_interop_server

TMPDIR=$(mktemp -d)
cp src/python/grpcio_tests/tests_py3_only/interop/Dockerfile.server $TMPDIR/
cp -rL bazel-bin/src/python/grpcio_tests/tests_py3_only/interop/xds_interop_server* $TMPDIR


(cd $TMPDIR;
    docker build \
        -t gcr.io/rbellevi-gke-dev/python_xds_interop_server:$VERSION \
        -f Dockerfile.server \
        .
)

rm -rf $TMPDIR
