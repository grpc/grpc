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

# "3.1" tag uses debian buster
FROM mcr.microsoft.com/dotnet/core/sdk:3.1

RUN apt-get update && apt-get install -y unzip && apt-get clean

# we only want to test dotnet core 3.1 runtime. This also allows us to keep
# this docker image minimal by not installing the other runtimes.
ENV SKIP_NET45_DISTRIBTEST=1
ENV SKIP_NETCOREAPP21_DISTRIBTEST=1
ENV SKIP_NET50_DISTRIBTEST=1
