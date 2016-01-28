#!/bin/bash

set -ex

cd $(dirname $0)

unzip "$EXTERNAL_GIT_ROOT/input_artifacts/csharp_nugets.zip" -d TestNugetFeed

# TODO(jtattermusch): replace the version number
./update_version.sh 0.13.0

nuget restore

xbuild DistribTest.sln

mono DistribTest/bin/Debug/DistribTest.exe
