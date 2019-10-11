#!/bin/bash
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
# Builds Grpc.AspNetCore.Server interop server in a base image.
set -e

mkdir -p /var/local/git
git clone /var/local/jenkins/grpc-dotnet /var/local/git/grpc-dotnet

# copy service account keys if available
cp -r /var/local/jenkins/service_account $HOME || true

cd /var/local/git/grpc-dotnet

# If needed, update dotnet SDK and put it on path
./build/get-dotnet.sh
# Normally we would source ./activate.sh
# to add dotnet to PATH, but that would only
# work for the build and not for a subsequent
# dotnet run from a different shell,
# so we create a symlink instead.
# TODO(jtattermusch): Come up with a cleaner solution.
if [ -f $(pwd)/.dotnet/dotnet ]
then
  ln -s $(pwd)/.dotnet/dotnet /usr/local/bin/dotnet
fi

# Cloning from a local path sets RepositoryUrl to a path and breaks Source Link.
# Override RepositoryUrl to a URL to fix Source Link. The value doesn't matter.
dotnet build --configuration Debug Grpc.DotNet.sln -p:RepositoryUrl=https://github.com/grpc/grpc-dotnet.git
