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
#
# This script is invoked by Jenkins and triggers a test run based on
# env variable settings.
#
# To prevent cygwin bash complaining about empty lines ending with \r
# we set the igncr option. The option doesn't exist on Linux, so we fallback
# to just 'set -ex' there.
# NOTE: No empty lines should appear in this file before igncr is set!
set -ex -o igncr || set -ex

if [ "$platform" == "linux" ]
then
  echo "building $language on Linux"

  if [ "$ghprbPullId" != "" ]
  then
    # if we are building a pull request, grab corresponding refs.
    FETCH_PULL_REQUEST_CMD="&& git fetch $GIT_URL refs/pull/$ghprbPullId/merge refs/pull/$ghprbPullId/head"
  fi

  # Make sure the CID file is gone.
  rm -f docker.cid

  # Run tests inside docker
  docker run --cidfile=docker.cid grpc/grpc_jenkins_slave bash -c -l "git clone --recursive $GIT_URL /var/local/git/grpc \
    && cd /var/local/git/grpc \
    $FETCH_PULL_REQUEST_CMD \
    && git checkout -f $GIT_COMMIT \
    && git submodule update \
    && nvm use 0.12 \
    && rvm use ruby-2.1 \
    && tools/run_tests/run_tests.py -t -l $language" || DOCKER_FAILED="true"

  DOCKER_CID=`cat docker.cid`
  if [ "$DOCKER_FAILED" == "" ]
  then
    echo "Docker finished successfully, deleting the container $DOCKER_CID"
    docker rm $DOCKER_CID
  else
    echo "Docker exited with failure, keeping container $DOCKER_CID."
    echo "You can SSH to the worker and use 'docker start CID' and 'docker exec -i -t CID bash' to debug the problem."
  fi

elif [ "$platform" == "windows" ]
then
  echo "building $language on Windows"

  # Prevent msbuild from picking up "platform" env variable, which would break the build
  unset platform

  # TODO(jtattermusch): integrate nuget restore in a nicer way.
  /cygdrive/c/nuget/nuget.exe restore vsprojects/grpc.sln
  /cygdrive/c/nuget/nuget.exe restore src/csharp/Grpc.sln

  python tools/run_tests/run_tests.py -t -l $language
else
  echo "Unknown platform $platform"
  exit 1
fi
