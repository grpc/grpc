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

FROM debian:bullseye

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
RUN chmod +x /usr/local/bin/apt-get-install-retry


RUN apt-get-install-retry --no-install-recommends openjdk-11-jdk-headless

WORKDIR /var/local/git/grpc-java/

# Define the default command.
CMD ["bash"]

