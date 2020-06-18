#!/bin/bash

echo "It's recommended that you run this script from a virtual environment."

set -e

BASEDIR=$(dirname "$0")
BASEDIR=$(realpath "$BASEDIR")/../..

(cd "$BASEDIR";
  pip install cython;
  python setup.py install;
  pushd tools/distrib/python/grpcio_tools;
    ../make_grpcio_tools.py
    GRPC_PYTHON_BUILD_WITH_CYTHON=1 pip install .
  popd;
  pushd src/python;
    for PACKAGE in ./grpcio_*; do
      pushd "${PACKAGE}";
        python setup.py preprocess;
        python setup.py install;
      popd;
    done
  popd;
)
