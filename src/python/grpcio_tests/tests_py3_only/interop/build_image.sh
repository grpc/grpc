#!/bin/bash

cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.."
pwd
docker build \
    -t gcr.io/rbellevi-gke-dev/python_xds_interop:5 \
    -f src/python/grpcio_tests/tests_py3_only/interop/Dockerfile \
    .
