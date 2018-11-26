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

FROM debian:stretch

# Install Git and basic packages.
RUN apt-get update && apt-get install -y \
  autoconf \
  autotools-dev \
  build-essential \
  bzip2 \
  ccache \
  curl \
  dnsutils \
  gcc \
  gcc-multilib \
  git \
  golang \
  gyp \
  lcov \
  libc6 \
  libc6-dbg \
  libc6-dev \
  libgtest-dev \
  libtool \
  make \
  perl \
  strace \
  python-dev \
  python-setuptools \
  python-yaml \
  telnet \
  unzip \
  wget \
  zip && apt-get clean

#================
# Build profiling
RUN apt-get update && apt-get install -y time && apt-get clean

#====================
# Python dependencies

# Install dependencies

RUN apt-get update && apt-get install -y \
    python-all-dev \
    python3-all-dev \
    python-pip

# Install Python packages from PyPI
RUN pip install --upgrade pip==10.0.1
RUN pip install virtualenv
RUN pip install futures==2.2.0 enum34==1.0.4 protobuf==3.5.2.post1 six==1.10.0 twisted==17.5.0

#================
# C# dependencies

# cmake >=3.6 needed to build grpc_csharp_ext
RUN apt-get update && apt-get install -y cmake && apt-get clean

# Install mono
RUN apt-get update && apt-get install -y apt-transport-https dirmngr && apt-get clean
RUN apt-key adv --no-tty --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys 3FA7E0328081BFF6A14DA29AA6A19B38D3D831EF
RUN echo "deb https://download.mono-project.com/repo/debian stable-stretch main" | tee /etc/apt/sources.list.d/mono-official-stable.list
RUN apt-get update && apt-get install -y \
    mono-devel \
    ca-certificates-mono \
    nuget \
    && apt-get clean

# Install dotnet SDK
ENV DOTNET_SDK_VERSION 2.1.500
RUN curl -sSL -o dotnet.tar.gz https://dotnetcli.blob.core.windows.net/dotnet/Sdk/$DOTNET_SDK_VERSION/dotnet-sdk-$DOTNET_SDK_VERSION-linux-x64.tar.gz \
    && mkdir -p /usr/share/dotnet \
    && tar -zxf dotnet.tar.gz -C /usr/share/dotnet \
    && rm dotnet.tar.gz \
    && ln -s /usr/share/dotnet/dotnet /usr/bin/dotnet


# Install .NET Core 1.0.5 Runtime (required to run netcoreapp1.0)
RUN curl -sSL -o dotnet105.tar.gz https://download.microsoft.com/download/2/4/A/24A06858-E8AC-469B-8AE6-D0CEC9BA982A/dotnet-debian-x64.1.0.5.tar.gz \
    && mkdir -p dotnet105_download \
    && tar zxf dotnet105.tar.gz -C dotnet105_download \
    && cp -r dotnet105_download/shared/Microsoft.NETCore.App/1.0.5/ /usr/share/dotnet/shared/Microsoft.NETCore.App/ \
    && rm -rf dotnet105_download/ dotnet105.tar.gz
RUN apt-get update && apt-get install -y libunwind8 && apt-get clean
# To prevent "Failed to initialize CoreCLR, HRESULT: 0x80131500" with .NET Core 1.0.5 runtime
RUN curl -sSL -o libicu52.deb http://http.us.debian.org/debian/pool/main/i/icu/libicu52_52.1-8+deb8u7_amd64.deb \
    && dpkg -i libicu52.deb \
    && rm libicu52.deb
RUN curl -sSL -o libssl100.deb http://security.debian.org/debian-security/pool/updates/main/o/openssl/libssl1.0.0_1.0.1t-1+deb8u10_amd64.deb \
    && dpkg -i libssl100.deb \
    && rm libssl100.deb


# Trigger the population of the local package cache
ENV NUGET_XMLDOC_MODE skip
RUN mkdir warmup \
    && cd warmup \
    && dotnet new \
    && cd .. \
    && rm -rf warmup


RUN mkdir /var/local/jenkins

# Define the default command.
CMD ["bash"]
