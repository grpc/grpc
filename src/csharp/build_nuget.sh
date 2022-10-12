#!/bin/bash
# Copyright 2020 The gRPC Authors
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

set -ex

cd "$(dirname "$0")"

mkdir -p ../../artifacts

# Collect protoc artifacts built by the previous build step
mkdir -p protoc_plugins
cp -r "${EXTERNAL_GIT_ROOT}"/input_artifacts/protoc_* protoc_plugins || true

# Add current timestamp to dev nugets
./nuget_helpers/expand_dev_version.sh

# For building the nugets we normally need native libraries and binaries
# built on multiple different platforms (linux, mac, windows), which makes
# it difficult to support a local build of the nuget.
# To allow simple local builds (restricted to a single platform),
# we provide a way of building "partial" nugets that only include artifacts
# that can be built locally on a given platform (e.g. linux), and
# contain placeholders (empty files) for artifacts that normally need
# to be built on a different platform. Because such nugets obviously
# only work on a single platform (and are broken on other platform),
# whenever we are building such nugets, we clearly mark them as
# "singleplatform only" to avoid mixing them up with the full "multiplatform"
# nugets by accident.
if [ "${GRPC_CSHARP_BUILD_SINGLE_PLATFORM_NUGET}" != "" ]
then
  # create placeholders for artifacts that can't be built
  # on the current platform.
  ./nuget_helpers/create_fake_native_artifacts.sh || true

  # add a suffix to the nuget's version
  # to avoid confusing the package with a full nuget package.
  # NOTE: adding the suffix must be done AFTER expand_dev_version.sh has run.
  sed -ibak "s/<\/GrpcCsharpVersion>/-singleplatform<\/GrpcCsharpVersion>/" build/dependencies.props
fi

dotnet restore Grpc.sln

dotnet pack --configuration Release Grpc.Tools --output ../../artifacts

# Create a zipfile with all the nugets we just created
cd ../../artifacts
zip csharp_nugets_windows_dotnetcli.zip *.nupkg
