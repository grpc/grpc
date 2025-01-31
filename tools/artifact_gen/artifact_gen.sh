#!/bin/bash

set -ex

# PHASE 0: query bazel for information we'll need
cd $(dirname $0)/../..
tools/bazel query --noimplicit_deps --output=xml 'deps(test/...)' > tools/artifact_gen/test_deps.xml

# PHASE 1: generate artifacts
cd tools/artifact_gen
bazel run :artifact_gen -- \
	`pwd`/test_deps.xml

