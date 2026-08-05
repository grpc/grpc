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

#=================
# Install apt-get-install-retry wrapper script
#=================
#  Create wrapper for apt-get

RUN /bin/bash -c 'echo -e "#!/usr/bin/env bash\n\
# Copyright 2026 The gRPC Authors\n\
\n\
PS4=\"+ \$(date +[%H:%M:%S_%Z]) \"\n\
set -xeuo pipefail\n\
\n\
MAX_RETRIES=4\n\
delay=3\n\
\n\
for i in \$(seq 1 \$MAX_RETRIES); do\n\
  if apt-get update && apt-get install -y \"\$@\"; then\n\
    break\n\
  fi\n\
  sleep \$((delay**i))\n\
done\n\
" > /usr/local/bin/apt-get-install-retry'
RUN chmod +x /usr/local/bin/apt-get-install-retryRUN apt-get-install-retry clang curl git

WORKDIR /grpc
COPY . .

RUN /grpc/tools/bazel build test/cpp/interop:observability_interop_test



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
