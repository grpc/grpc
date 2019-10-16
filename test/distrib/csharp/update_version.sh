#!/bin/bash
# Copyright 2015 gRPC authors.
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

set -e

cd "$(dirname "$0")"

CSHARP_VERSION="$1"
if [ "$CSHARP_VERSION" == "auto" ]
then
  # autodetect C# version from the name of Grpc.Core.0.0.0-x.nupkg file
  # TODO: find a better shellcheck-compliant way to write the following line
  # shellcheck disable=SC2010
  CSHARP_VERSION=$(ls TestNugetFeed | grep -m 1 '^Grpc\.Core\.[0-9].*\.nupkg$' | sed s/^Grpc\.Core\.// | sed s/\.nupkg$// | sed s/\.symbols$//)
  echo "Autodetected nuget ${CSHARP_VERSION}"
fi

# Replaces version placeholder with value provided as first argument.
sed -ibak "s/__GRPC_NUGET_VERSION__/${CSHARP_VERSION}/g" DistribTest/packages.config DistribTest/DistribTest.csproj DistribTest/DistribTestDotNet.csproj
