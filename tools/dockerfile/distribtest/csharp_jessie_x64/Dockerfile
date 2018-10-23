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

FROM debian:jessie

RUN apt-get update && apt-get install -y apt-transport-https && apt-get clean

RUN apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys 3FA7E0328081BFF6A14DA29AA6A19B38D3D831EF
RUN echo "deb https://download.mono-project.com/repo/debian stable-jessie main" | tee /etc/apt/sources.list.d/mono-official-stable.list

RUN apt-get update && apt-get install -y \
    mono-devel \
    nuget \
    && apt-get clean

RUN apt-get update && apt-get install -y unzip && apt-get clean

# Make sure the mono certificate store is up-to-date to prevent issues with nuget restore
RUN apt-get update && apt-get install -y curl && apt-get clean
RUN curl https://curl.haxx.se/ca/cacert.pem > ~/cacert.pem && cert-sync ~/cacert.pem && rm -f ~/cacert.pem
