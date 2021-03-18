#!/bin/bash

# TODO: Rename this file.
# TODO: Rename Dockerfile.

VERSION=9

cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.."
docker build \
    -t gcr.io/rbellevi-gke-dev/python_xds_interop:$VERSION \
    -f src/python/grpcio_tests/tests_py3_only/interop/Dockerfile \
    .
