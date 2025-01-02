#!/bin/bash
# Copyright 2021 The gRPC Authors
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

# This wrapper allows running concurrent instances of bazel build/test by running
# bazel under a docker container.
# This is especially useful for running bazel RBE builds, since inside
# the container, bazel won't have access to local build cache.
# Access to the local workspace is provided by mounting the workspace
# as a volume to the docker container. That also means that any changes
# in the workspace will be visible to the bazel instance running inside
# the container (but also this is a similar scenario to making changes
# to local files when bazel is running normally).

# Usage:
# tools/docker_runners/examples/concurrent_bazel.sh ANY_NORMAL_BAZEL_FLAGS_HERE

set -ex

# change to grpc repo root
cd "$(dirname "$0")/../../.."

# use the default docker image used for bazel builds
export DOCKERFILE_DIR=tools/dockerfile/test/bazel

# Bazel RBE uses application default credentials from localhost to authenticate with RBE servers. Use a trick to make the credentials accessible from inside the docker container."
APPLICATION_DEFAULT_CREDENTIALS_DIR="$HOME/.config/gcloud"
export DOCKER_EXTRA_ARGS="-v=${APPLICATION_DEFAULT_CREDENTIALS_DIR}:/application_default_credentials:ro -e=GOOGLE_APPLICATION_CREDENTIALS=/application_default_credentials/application_default_credentials.json"

# Run bazel inside a docker container (local git workspace will be mounted to the container)
tools/docker_runners/run_in_docker.sh bazel "$@"
