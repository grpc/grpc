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

FROM ubuntu:22.04

RUN apt-get update && apt-get install -y apt-transport-https ca-certificates gnupg wget unzip && apt-get clean

RUN wget https://packages.microsoft.com/config/ubuntu/22.04/packages-microsoft-prod.deb -O packages-microsoft-prod.deb

RUN dpkg -i packages-microsoft-prod.deb

RUN rm packages-microsoft-prod.deb

RUN apt-get update && apt-get install -y dotnet-sdk-7.0
# Set up links so that the dotnet command can find the installed SDK
RUN for f in /usr/share/dotnet/* ; do ln -s $f /usr/lib/dotnet ; done

# we only want to test dotnet core 6.0 runtime. This also allows us to keep
# this docker image minimal by not installing the other runtimes.
ENV SKIP_NET45_DISTRIBTEST=1
ENV SKIP_NETCOREAPP21_DISTRIBTEST=1
ENV SKIP_NETCOREAPP31_DISTRIBTEST=1
ENV SKIP_NET50_DISTRIBTEST=1
