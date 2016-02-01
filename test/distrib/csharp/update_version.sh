#!/bin/bash

set -e

cd $(dirname $0)

# Replaces version placeholder with value provided as first argument.
sed -i "s/__GRPC_NUGET_VERSION__/$1/g" DistribTest/packages.config DistribTest/DistribTest.csproj
