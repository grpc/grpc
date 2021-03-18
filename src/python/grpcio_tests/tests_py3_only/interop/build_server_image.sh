#!/bin/bash

set -x

VERSION=1

cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.."

docker build \
    -t gcr.io/rbellevi-gke-dev/python_xds_interop_server:$VERSION \
    -f src/python/grpcio_tests/tests_py3_only/interop/Dockerfile.server \
    .
