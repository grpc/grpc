# Copyright 2015 gRPC authors.
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

FROM ubuntu:24.04

RUN apt update && apt -y upgrade
RUN apt install -y apt-transport-https ca-certificates gnupg wget unzip && apt clean

# Install dotnet SDK
RUN wget https://packages.microsoft.com/config/ubuntu/22.04/packages-microsoft-prod.deb -O packages-microsoft-prod.deb \
    && dpkg -i packages-microsoft-prod.deb \
    && rm packages-microsoft-prod.deb \
    && apt update \
    && apt install -y dotnet-sdk-8.0 \
    && apt clean

# we only want to test dotnet core runtime. This also allows us to keep
# this docker image minimal by not installing the other runtimes.
ENV SKIP_NET45_DISTRIBTEST=1
ENV SKIP_NETCOREAPP21_DISTRIBTEST=1
ENV SKIP_NETCOREAPP31_DISTRIBTEST=1
ENV SKIP_NET50_DISTRIBTEST=1
