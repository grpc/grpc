#!/usr/bin/env bash
# Copyright 2017, Google Inc.
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
# This script invoke run_interop_tests in manual mode to create test cases
# and docker images.  These test cases are then stored in the docker images
# and uploaded to Google Container Registry for running version compatibility
# tests at a later time.
set -ex

export LANG=en_US.UTF-8

# Enter the gRPC repo root
cd $(dirname $0)/../../..

_COMMON_FLAGS="--use_docker --manual_run"
# TODO(yongni): add other tests.
tools/run_tests/run_interop_tests.py -l java --cloud_to_prod $_COMMON_FLAGS

# Outputs of run_interop_tests.py.
_DOCKER_IMG_LIST='docker_images'
_CLIENT_CMDS='interop_client_cmds.sh'
_REPORT='report.xml'

images=$(cat $_DOCKER_IMG_LIST)
[ -n "$images" ] || (echo "No image found"; exit 1)
[ -e "$_CLIENT_CMDS" ] || (echo "$_CLIENT_CMDS not found"; exit 1)

echo "Uploading images: $image"
tools/gcp/utils/gcr_upload.py --gcr_tag=comp_latest --with_files=$_CLIENT_CMDS \
  --images=$images

# Clean up
xargs -I % docker rmi % < $_DOCKER_IMG_LIST
