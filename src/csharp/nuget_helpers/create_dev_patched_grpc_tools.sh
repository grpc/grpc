#!/bin/bash
# Copyright 2022 The gRPC Authors
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

# ***** FOR DEV TESTING ONLY *****
# Create a grpc.tools NuGet package by patching an existing NuGet package
# and replacing some of the files with dev files.
# At the moment this just replaces the MSBuild files.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# parse args
# -p original package
# -w working directory (optional - default: /tmp)
# -d destination directory (optional - default: . )
# -v new package version number (optional - default: 9.99.0-dev)
# -s grpc.tools source directory (optional - default: ../Grpc.Tools relative to this dir)

while [[ $# -gt 0 ]]; do
    case $1 in
        -p)
            ORIG_PACKAGE="$2"
            shift # past argument
            shift # past value
            ;;
        -w)
            WORKING_DIR="$2"
            shift # past argument
            shift # past value
            ;;
        -d)
            DESTINATION_DIR="$2"
            shift # past argument
            shift # past value
            ;;
        -v)
            VERSION_NUMBER="$2"
            shift # past argument
            shift # past value
            ;;
        -s)
            SRC_DIR="$2"
            shift # past argument
            shift # past value
            ;;
        *)
            echo "Unknown argument $1"
            exit 1
    esac
done

[[ ! -f $ORIG_PACKAGE ]] && { echo "-p original package '$ORIG_PACKAGE' not found"; exit 1; }

if [[ ! -d $DESTINATION_DIR ]];
then
    DESTINATION_DIR=.
fi

if [[ -z $SRC_DIR ]];
then
    SRC_DIR=$(realpath $SCRIPT_DIR/../Grpc.Tools)
fi

[[ ! -d $SRC_DIR ]] && { echo "Source directory '$SRC_DIR' not found"; exit 1; }

if [[ -z $WORKING_DIR ]];
then
    WORKING_DIR=$TMPDIR
fi

[[ ! -d $WORKING_DIR ]] && { echo "-w working directory '$WORKING_DIR' not found"; exit 1; }

if [[ -z $VERSION_NUMBER ]];
then
    VERSION_NUMBER=9.99.0-dev
fi

echo "Using"
echo "     Original package: $ORIG_PACKAGE"
echo " Dest dir for package: $DESTINATION_DIR"
echo "           Source dir: $SRC_DIR"
echo "       Version number: $VERSION_NUMBER"
echo "          Working dir: $WORKING_DIR"

set -ex

UNPACK_DIR=$WORKING_DIR/toolspatch
# clean working dir
if [[ -n "$UNPACK_DIR" && -d $UNPACK_DIR ]];
then
    if [[ -d $UNPACK_DIR.bak ]];
    then
        rm -rf $UNPACK_DIR.bak
    fi
    mv $UNPACK_DIR $UNPACK_DIR.bak
fi

# extract files from original
unzip $ORIG_PACKAGE -d $UNPACK_DIR

# remove some files
rm -rf $UNPACK_DIR/package
rm -rf $UNPACK_DIR/_rels
rm $UNPACK_DIR/'[Content_Types].xml'
rm $UNPACK_DIR/.signature.p7s

# edit version number in NuSpec
sed -i "s/<version>.*<\/version>/<version>$VERSION_NUMBER<\/version>/" $UNPACK_DIR/Grpc.Tools.nuspec

# copy in new files
cp -fv $SRC_DIR/build/_grpc/_Grpc.Tools.props $UNPACK_DIR/build/_grpc/
cp -fv $SRC_DIR/build/_grpc/_Grpc.Tools.targets $UNPACK_DIR/build/_grpc/
cp -fv $SRC_DIR/build/_protobuf/Google.Protobuf.Tools.props $UNPACK_DIR/build/_protobuf/
cp -fv $SRC_DIR/build/_protobuf/Google.Protobuf.Tools.targets $UNPACK_DIR/build/_protobuf/

# build the new package
nuget pack -OutputDirectory "$DESTINATION_DIR" $UNPACK_DIR/Grpc.Tools.nuspec
