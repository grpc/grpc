#!/bin/bash
# Copyright 2026 gRPC authors.
set -ex

# avoid slow finalization after the script has exited.
source $(dirname $0)/../../../tools/internal_ci/helper_scripts/move_src_tree_and_respawn_itself_rc

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc
source tools/internal_ci/helper_scripts/prepare_ccache_rc

python3 tools/run_tests/run_tests_openssl102.py || FAILED="true"

echo 'Exiting gRPC test script.'

if [ "$FAILED" != "" ]
then
  exit 1
fi
