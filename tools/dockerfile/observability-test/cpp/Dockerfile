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


#
# Stage 1: Build the interop test client and server
#

FROM debian:bullseye as build

RUN apt-get update && apt-get install -y clang curl git

WORKDIR /grpc
COPY . .

RUN git submodule update --init --recursive && \
  /grpc/tools/bazel build test/cpp/interop:observability_interop_test



#
# Stage 2:
#
# - Copy only the necessary files to reduce Docker image size.
# - Have an ENTRYPOINT script which will launch the interop test client or server
#   with the given parameters.
#

FROM debian:bullseye

ENV GRPC_TRACE environment_autodetect
ENV GRPC_VERBOSITY debug


WORKDIR /usr/share/grpc
COPY --from=build /grpc/etc/roots.pem .


WORKDIR /grpc/bazel-bin/test/cpp/interop
COPY --from=build /grpc/bazel-bin/test/cpp/interop/observability_interop_server .
COPY --from=build /grpc/bazel-bin/test/cpp/interop/observability_interop_client .

WORKDIR /grpc/tools/dockerfile/observability-test/cpp
COPY --from=build /grpc/tools/dockerfile/observability-test/cpp/run.sh .

ENTRYPOINT ["/grpc/tools/dockerfile/observability-test/cpp/run.sh"]
