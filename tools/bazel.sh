#!/bin/bash
# Copyright 2019 The gRPC Authors
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


# Keeping up with Bazel's breaking changes is currently difficult.
# This script wraps calling bazel by downloading the currently
# supported version, and then calling it. This way, we can make sure
# that running bazel will always get meaningful results, at least
# until Bazel 1.0 is released.

set -e

VERSION=0.24.1

CWD=`pwd`
BASEURL=https://github.com/bazelbuild/bazel/releases/download/
cd `dirname $0`
TOOLDIR=`pwd`

case `uname -sm` in
  "Linux x86_64")
    suffix=linux-x86_64
    ;;
  "Darwin x86_64")
    suffix=darwin-x86_64
    ;;
  *)
    echo "Unsupported architecture: `uname -sm`"
    exit 1
    ;;
esac

filename=bazel-$VERSION-$suffix

if [ ! -x $filename ] ; then
  curl -L $BASEURL/$VERSION/$filename > $filename
  chmod a+x $filename
fi

cd $CWD
$TOOLDIR/$filename $@
