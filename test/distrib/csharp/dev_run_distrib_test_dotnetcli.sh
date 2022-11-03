#!/bin/bash
# Copyright 2022 gRPC authors.
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

#           ***** HELPER SCRIPT FOR DEV TESTING ONLY ******
#
# Run the C# distrib tests in a dev environment
#
# ** PRE-REQUISITES **
#
# - Assumes you have already copied a dev version of Grpc.Tools nuget package
#   to the TestNugetFeed directory.
#   NOTE: one way to create a dev nuget package is to use the script
#      src/csharp/nuget_helpers/create_dev_patched_grpc_tools.sh
#
# - You may want to clear the local NuGet cache to make sure the tests use
#   the correct nuget package if you have made changes but not changed the
#   version number:
#      dotnet nuget locals all --clear
#   or
#      nuget locals all -clear
#   Alternatively set a different directory for the local NuGet cache
#      export NUGET_PACKAGES=<some directory>

# Parameters
# -v nuget package version number
#     default: 9.99.0-dev
# -t test to run (can repeat multiple -t params)
#     detault: DistribTest
# -f frameworks (can repeat multiple -f params)
#     default frameworks: net45 netcoreapp21 netcoreapp31 net50
#
# On Windows use Git Bash or Cygwin terminal
#
# E.g.  ./dev_run_distrib_test_dotnetcli.sh -v 2.99.0-dev -f netcoreapp31 -t TestAtPath
#

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# determine OS
OS="unknown"
case "$OSTYPE" in
  darwin*)  OS="OSX" ;;
  linux*)   OS="LINUX" ;;
  msys*)    OS="WINDOWS" ;; # Git Bash
  cygwin*)  OS="WINDOWS" ;; # Cygwin
  *)        OS="unknown" ;;
esac

TEST_DIRS=()
FRAMEWORKS=()
while [[ $# -gt 0 ]]; do
    case $1 in
        -t)
            TEST_DIRS+=("$2")
            shift # past argument
            shift # past value
            ;;
        -v)
            VERSION_NUMBER="$2"
            shift # past argument
            shift # past value
            ;;
        -f)
            FRAMEWORKS+=("$2")
            shift # past argument
            shift # past value
            ;;
        *)
            echo "Unknown argument $1"
            exit 1
    esac
done

if [[ -z $VERSION_NUMBER ]];
then
    VERSION_NUMBER=9.99.0-dev
fi

if [[ ${#TEST_DIRS[*]} == 0 ]];
then
  TEST_DIRS=("DistribTest")
fi

if [[ ${#FRAMEWORKS[*]} == 0 ]];
then
  FRAMEWORKS=("net45" "netcoreapp21" "netcoreapp31" "net50")
fi

echo "Using test parameters:"
echo "    Version number: $VERSION_NUMBER"
echo "  Test directories: ${TEST_DIRS[*]}"
echo "        Frameworks: ${FRAMEWORKS[*]}"
echo ""

#set -ex

# set environment variables expected in the proj files for each framework
export SKIP_NET45_DISTRIBTEST=1
export SKIP_NETCOREAPP21_DISTRIBTEST=1
export SKIP_NETCOREAPP31_DISTRIBTEST=1
export SKIP_NET50_DISTRIBTEST=1

for framework in ${FRAMEWORKS}; do
  if [[ $framework == "net45" ]]
  then
    unset SKIP_NET45_DISTRIBTEST
  elif [[ $framework == "netcoreapp21" ]]
  then
    unset SKIP_NETCOREAPP21_DISTRIBTEST
  elif [[ $framework == "netcoreapp31" ]]
  then
    unset SKIP_NETCOREAPP31_DISTRIBTEST
  elif [[ $framework == "net50" ]]
  then
    unset SKIP_NET50_DISTRIBTEST
  fi
done

echo "Framework env variables:"
echo "SKIP_NET45_DISTRIBTEST=$SKIP_NET45_DISTRIBTEST"
echo "SKIP_NETCOREAPP21_DISTRIBTEST=$SKIP_NETCOREAPP21_DISTRIBTEST"
echo "SKIP_NETCOREAPP31_DISTRIBTEST=$SKIP_NETCOREAPP31_DISTRIBTEST"
echo "SKIP_NET50_DISTRIBTEST=$SKIP_NET50_DISTRIBTEST"
echo ""

# run test in each test directory
for test_dir in ${TEST_DIRS}; do
  dir=$(realpath $SCRIPT_DIR/$test_dir)
  echo ">>>>> Test directory: $dir"
  echo ""
  [[ ! -d $dir ]] && { echo "Test dir '$dir' not found"; exit 1; }
  cd $dir

  # update version number for Grpc.Tools in project file
  sed -ibak "s/Include=\"Grpc.Tools\" Version=\".*\"/Include=\"Grpc.Tools\" Version=\"$VERSION_NUMBER\"/g" DistribTestDotNet.csproj

  # restore the project
  echo "Restore..."
  dotnet restore DistribTestDotNet.csproj
  [[ $? -ne 0 ]] && { echo "dotnet restore command failed"; exit 1; }

  # clean
  echo "Clean..."
  dotnet clean DistribTestDotNet.csproj
  [[ $? -ne 0 ]] && { echo "dotnet clean command failed"; exit 1; }

  # build
  echo "Build..."
  dotnet build DistribTestDotNet.csproj
  [[ $? -ne 0 ]] && { echo "dotnet build command failed"; exit 1; }

  # check what was build
  echo "Generated CS files..."
  find obj -name "Test*.cs"
  echo "Output files..."
  ls -R bin
  echo ""

  # publish and run for each framework

  if [ "${SKIP_NET45_DISTRIBTEST}" != "1" ]
  then
    echo ""
    echo ">>>>> Framework net45 (mono)"
    echo "publish..."
    dotnet publish -f net45 DistribTestDotNet.csproj
    [[ $? -ne 0 ]] && { echo "dotnet publish command failed"; exit 1; }

    # .NET 4.5 target after dotnet build
    echo "running..."
    mono bin/Debug/net45/DistribTestDotNet.exe
    [[ $? -ne 0 ]] && { echo "mono running DistribTestDotNet.exe command failed"; exit 1; }

    # .NET 4.5 target after dotnet publish
    echo "running publish..."
    mono bin/Debug/net45/publish/DistribTestDotNet.exe
    [[ $? -ne 0 ]] && { echo "mono running published DistribTestDotNet.exe command failed"; exit 1; }
  fi

  if [ "${SKIP_NETCOREAPP21_DISTRIBTEST}" != "1" ]
  then
    echo ""
    echo ">>>>> Framework netcoreapp21"
    echo "publish..."
    dotnet publish -f netcoreapp2.1 DistribTestDotNet.csproj
    [[ $? -ne 0 ]] && { echo "dotnet publish command failed"; exit 1; }

    # .NET Core target after dotnet build
    echo "running..."
    dotnet exec bin/Debug/netcoreapp2.1/DistribTestDotNet.dll
    [[ $? -ne 0 ]] && { echo "running DistribTestDotNet.dll command failed"; exit 1; }

    # .NET Core target after dotnet publish
    echo "running publish..."
    dotnet exec bin/Debug/netcoreapp2.1/publish/DistribTestDotNet.dll
    [[ $? -ne 0 ]] && { echo "running published DistribTestDotNet.dll command failed"; exit 1; }
  fi

  if [ "${SKIP_NETCOREAPP31_DISTRIBTEST}" != "1" ]
  then
    echo ""
    echo ">>>>> Framework netcoreapp31"
    echo "publish..."
    dotnet publish -f netcoreapp3.1 DistribTestDotNet.csproj
    [[ $? -ne 0 ]] && { echo "dotnet publish command failed"; exit 1; }

    # .NET Core target after dotnet build
    echo "running..."
    dotnet exec bin/Debug/netcoreapp3.1/DistribTestDotNet.dll
    [[ $? -ne 0 ]] && { echo "running DistribTestDotNet.dll command failed"; exit 1; }

    # .NET Core target after dotnet publish
    echo "running publish..."
    dotnet exec bin/Debug/netcoreapp3.1/publish/DistribTestDotNet.dll
    [[ $? -ne 0 ]] && { echo "running published DistribTestDotNet.dll command failed"; exit 1; }
  fi

  if [ "${SKIP_NET50_DISTRIBTEST}" != "1" ]
  then
    echo ""
    echo ">>>>> Framework net50"
    echo "publish..."
    dotnet publish -f net5.0 DistribTestDotNet.csproj
    [[ $? -ne 0 ]] && { echo "dotnet publish command failed"; exit 1; }

    # .NET Core target after dotnet build
    echo "running..."
    dotnet exec bin/Debug/net5.0/DistribTestDotNet.dll
    [[ $? -ne 0 ]] && { echo "running DistribTestDotNet.dll command failed"; exit 1; }

    # .NET Core target after dotnet publish
    echo "running published..."
    dotnet exec bin/Debug/net5.0/publish/DistribTestDotNet.dll
    [[ $? -ne 0 ]] && { echo "running published DistribTestDotNet.dll command failed"; exit 1; }

    if [[ $OS == "LINUX" ]]
    then
      echo "publish single binary..."
      dotnet publish -r linux-x64 -f net5.0 DistribTestDotNet.csproj -p:PublishSingleFile=true --self-contained true --output net5_singlefile_publish
      [[ $? -ne 0 ]] && { echo "dotnet publish to single file command failed"; exit 1; }

      # binary generated by the single file publish
      echo "running single binary..."
      ./net5_singlefile_publish/DistribTestDotNet
      [[ $? -ne 0 ]] && { echo "running single file command failed"; exit 1; }

    elif [[ $OS == "WINDOWS" ]]
    then
      echo "publish single binary..."
      dotnet publish -r win-x64 -f net5.0 DistribTestDotNet.csproj -p:PublishSingleFile=true --self-contained true --output net5_singlefile_publish
      [[ $? -ne 0 ]] && { echo "dotnet publish to single file command failed"; exit 1; }

      # binary generated by the single file publish
      echo "running single binary..."
      ./net5_singlefile_publish/DistribTestDotNet.exe
      [[ $? -ne 0 ]] && { echo "running single file command failed"; exit 1; }
    fi
  fi

  echo "Finished test $dir"

done # tests loop
