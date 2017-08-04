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

set -ex

cd $(dirname $0)

unzip -o "$EXTERNAL_GIT_ROOT/input_artifacts/csharp_nugets_windows_dotnetcli.zip" -d TestNugetFeed

./update_version.sh auto

cd DistribTest

# TODO(jtattermusch): make sure we don't pollute the global nuget cache with
# the nugets being tested.
dotnet restore

dotnet build
dotnet publish

# .NET 4.5 target after dotnet build
mono bin/Debug/net45/*-x64/DistribTest.exe

# .NET 4.5 target after dotnet publish
mono bin/Debug/net45/*-x64/publish/DistribTest.exe

# .NET Core target after dotnet build
dotnet exec bin/Debug/netcoreapp1.0/DistribTest.dll

# .NET Core target after dotnet publish
dotnet exec bin/Debug/netcoreapp1.0/publish/DistribTest.dll
