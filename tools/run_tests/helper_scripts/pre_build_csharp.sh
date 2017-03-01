#!/bin/bash
# Copyright 2015, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -ex

# cd to gRPC csharp directory
cd $(dirname $0)/../../../src/csharp

root=`pwd`

if [ -x "$(command -v nuget)" ]
then
  # TODO(jtattermusch): Get rid of this hack. See #8034
  # Restoring Nuget packages by packages rather than by solution because of
  # inability to restore by solution with Nuget client 3.4.4
  # Moving into each directory to let the restores work based on per-project packages.config files
  cd Grpc.Auth
  nuget restore -PackagesDirectory ../packages
  cd ..

  cd Grpc.Core.Tests
  nuget restore -PackagesDirectory ../packages
  cd ..

  cd Grpc.Core
  nuget restore -PackagesDirectory ../packages
  cd ..

  cd Grpc.Examples.MathClient
  nuget restore -PackagesDirectory ../packages
  cd ..

  cd Grpc.Examples.MathServer
  nuget restore -PackagesDirectory ../packages
  cd ..

  cd Grpc.Examples
  nuget restore -PackagesDirectory ../packages
  cd ..

  cd Grpc.HealthCheck.Tests
  nuget restore -PackagesDirectory ../packages
  cd ..

  cd Grpc.HealthCheck
  nuget restore -PackagesDirectory ../packages
  cd ..

  cd Grpc.IntegrationTesting.Client
  nuget restore -PackagesDirectory ../packages
  cd ..

  cd Grpc.IntegrationTesting.QpsWorker
  nuget restore -PackagesDirectory ../packages
  cd ..

  cd Grpc.IntegrationTesting.StressClient
  nuget restore -PackagesDirectory ../packages
  cd ..

  cd Grpc.IntegrationTesting
  nuget restore -PackagesDirectory ../packages
  cd ..
fi
