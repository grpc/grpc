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

FROM debian:10

RUN apt-get update && apt-get install -y apt-transport-https ca-certificates curl dirmngr gnupg unzip wget && apt-get clean

# Install mono
RUN apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys 3FA7E0328081BFF6A14DA29AA6A19B38D3D831EF \
    && echo "deb https://download.mono-project.com/repo/debian stable-buster main" | tee /etc/apt/sources.list.d/mono-official-stable.list \
    && apt-get update && apt-get install -y mono-devel nuget \
    && apt-get clean

# Install dotnet SDK
RUN wget https://packages.microsoft.com/config/debian/10/packages-microsoft-prod.deb \
    && dpkg -i packages-microsoft-prod.deb \
    && apt update \
    && apt install -y dotnet-sdk-3.1 \
    && apt-get clean

# Make sure the mono certificate store is up-to-date to prevent issues with nuget restore
RUN curl https://curl.haxx.se/ca/cacert.pem > ~/cacert.pem && cert-sync ~/cacert.pem && rm -f ~/cacert.pem

# we have a separate distribtest for netcoreapp2.1
ENV SKIP_NETCOREAPP21_DISTRIBTEST=1
# we have a separate distribtest for net5.0
ENV SKIP_NET50_DISTRIBTEST=1
