#!/bin/bash
# Copyright 2020 gRPC authors.
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

# PATHS specifies the type of output for the go protobuf plugin. See the
# documentation at: https://github.com/golang/protobuf#parameters.
PATHS="paths=source_relative"

# PLUGINS defines the plugins for the go protobuf plugin.
PLUGINS="plugins=grpc"

# MODS contains the map of protobuf imports to actual compiled go packages. It
# should not be modified by hand. Use define-import to add an entry.
MODS=""

# INCLUDES contains the list of include paths for the protobuf compiler. It
# should not be modified by hand. Use include to add an entry.
INCLUDES=""

# define-import accepts two arguments: the import path found in the proto and
# the path of the go code that the go plugin should actually use in the import
# statement.
function define-import {
  if [ -z "$MODS" ]
  then
    MODS="M$1=$2"
  else
    MODS="$MODS,M$1=$2"
  fi
}

# include adds a formatted include path that can be passed to protoc to the
# $INCLUDES variable.
function include {
  INCLUDES="$INCLUDES -I $1"
}

# Google API protos are needed for the long-running operations service messages
# and annotations.
#
# Unfortunately, the version in ../third_party may not match
# the compiled version of the define-import statement. There are 2 possible
# solutions: force grpc to remain up-to-date or download the latest protobuf
# file from github and use that to compile. Both are not desirable.
include "../../third_party/googleapis"
define-import "google/longrunning/operations.proto" "google.golang.org/genproto/googleapis/longrunning"

# The protobufs found in src/proto/grpc/testing are needed by reference.
# Due to their package names, the entire grpc must be included.
#
# As with the Google API protos, the version in grpc/grpc may not match the
# compiled version referenced in grpc/grpc-go. That said, the grpc-go team felt
# this was the best approach.
#
# A possible solution would be to download the protobufs from grpc/grpc-proto on
# github and use them to compile. However, this is also not desirable.
include "../../"  # root of grpc/grpc
define-import "src/proto/grpc/testing/control.proto" "github.com/codeblooded/grpc-proto/genproto/grpc/testing"

# The local location of the proto files.
include "./"

CMD="protoc $INCLUDES --go_out=$PATHS,$PLUGINS,$MODS:. scheduling/v1/*.proto"
echo "$CMD"
eval "$CMD"

