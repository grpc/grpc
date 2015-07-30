#!/bin/sh

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

set -e

#clean up any old docker files and start mirroring repository if not started already
sudo docker rmi -f grpc/cxx || true
sudo docker rmi -f grpc/base || true
sudo docker rmi -f 0.0.0.0:5000/grpc/base || true
sudo docker run -d -e GCS_BUCKET=docker-interop-images  -e STORAGE_PATH=/admin/docker_images -p 5000:5000 google/docker-registry || true

#prepare building by pulling down base images and necessary files
sudo docker pull 0.0.0.0:5000/grpc/base
sudo docker tag -f 0.0.0.0:5000/grpc/base grpc/base
gsutil cp -R gs://docker-interop-images/admin/service_account tools/dockerfile/grpc_cxx
gsutil cp -R gs://docker-interop-images/admin/cacerts tools/dockerfile/grpc_cxx

#build docker file, add more languages later
sudo docker build --no-cache -t grpc/cxx tools/dockerfile/grpc_cxx
