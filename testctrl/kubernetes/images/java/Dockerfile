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

# Install Git and basic packages.
RUN apt-get update && apt-get install -y \
  curl \
  git \
  make \
  python-dev \
  python-setuptools \
  python-yaml

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
RUN pip install --upgrade pip==19.3.1
RUN pip install virtualenv
RUN pip install futures==2.2.0 enum34==1.0.4 protobuf==3.5.2.post1 six==1.10.0 twisted==17.5.0

#==================
# Java dependencies
RUN echo "deb http://archive.debian.org/debian/ jessie-backports main contrib non-free" > /etc/apt/sources.list.d/jessie-backports.list && \
    echo 'Acquire::Check-Valid-Until no;' > /etc/apt/apt.conf.d/99no-check-valid-until && \
    apt-get update && \
    apt-get install -y --no-install-recommends -t jessie-backports openjdk-8-jdk-headless && \
    apt-get clean

#================
# Clone gRPC repo
RUN mkdir /grpc
RUN git clone --depth 1 https://github.com/grpc/grpc-java.git /grpc/grpc-java
WORKDIR /grpc/grpc-java

#========================
# Build performance tests
RUN ./gradlew -PskipCodegen=true -PskipAndroid=true :grpc-benchmarks:installDist

#============================================
# Set default benchmark environment variables
ENV DRIVER_PORT=10000
ENV WORKER_KIND=client
ENV SERVER_PORT=10010

#==============================
# Copy and prepare start script
COPY run.sh .
RUN chmod a+x run.sh

#======================
# Expose possible ports
EXPOSE $DRIVER_PORT
EXPOSE $SERVER_PORT

#===========
# Entrypoint
CMD ["./run.sh"]

