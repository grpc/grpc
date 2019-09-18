# Copyright 2017 gRPC authors.
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

# Docker file for building gRPC Raspbian binaries

# TODO(https://github.com/grpc/grpc/issues/19199): Move off of this base image.
FROM quay.io/grpc/raspbian_armv7

# Place any extra build instructions between these commands
# Recommend modifying upstream docker image (quay.io/grpc/raspbian_armv7)
# for build steps because running them under QEMU is very slow
# (https://github.com/kpayson64/armv7hf-debian-qemu)
RUN [ "cross-build-start" ]
RUN find /usr/local/bin -regex '.*python[0-9]+\.[0-9]+' | xargs -n1 -i{} bash -c "{} -m pip install --upgrade wheel setuptools"
RUN [ "cross-build-end" ]
