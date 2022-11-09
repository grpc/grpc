# Copyright 2021 gRPC authors.
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

# Docker file for running IWYU.
# Updated: 2022-11-03

FROM silkeh/clang:15-bullseye

# Install prerequisites for the iwyu script
RUN apt-get update && apt-get install -y python3 jq git cmake python zlib1g-dev libtinfo-dev libclang-15-dev && apt-get clean
ADD iwyu.sh /

# When running locally, we'll be impersonating the current user, so we need
# to make the script runnable by everyone.
RUN chmod a+rx /iwyu.sh

CMD ["echo 'Run with tools/distrib/iwyu.sh'"]
