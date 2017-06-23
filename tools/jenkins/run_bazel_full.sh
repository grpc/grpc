#!/usr/bin/env bash
# Copyright 2017 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Test full Bazel
#
# NOTE: No empty lines should appear in this file before igncr is set!
set -ex -o igncr || set -ex

export DOCKERFILE_DIR=tools/dockerfile/test/bazel
export DOCKER_RUN_SCRIPT=tools/jenkins/run_bazel_full_in_docker.sh
# Warn PR author if they make a change to the bazel directory
tools/run_tests/python_utils/check_bazel_dir.py
exec tools/run_tests/dockerize/build_and_run_docker.sh
