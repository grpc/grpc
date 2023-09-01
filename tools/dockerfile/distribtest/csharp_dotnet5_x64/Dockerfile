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

# "5.0.x" tag uses debian buster
# we use the full version number to make docker image version updates explicit
FROM mcr.microsoft.com/dotnet/sdk:5.0.103

RUN apt-get update && apt-get install -y unzip && apt-get clean

# we only want to test dotnet 5 runtime. This also allows us to keep
# this docker image minimal by not installing the other runtimes.
ENV SKIP_NET45_DISTRIBTEST=1
ENV SKIP_NETCOREAPP21_DISTRIBTEST=1
ENV SKIP_NETCOREAPP31_DISTRIBTEST=1
