#!/bin/bash
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
