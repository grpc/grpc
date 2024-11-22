# Copyright 2023 The gRPC Authors
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

FROM ubuntu:20.04

# Workaround the "tzdata hangs apt-get install during docker image build" issue for ubuntu20.04
# by setting timezone to "Etc/UTC" in advance.
RUN echo "Etc/UTC" > /etc/timezone

RUN apt-get update && apt-get install -y build-essential curl git time wget zip && apt-get clean

#====================
# run_tests.py python dependencies

# Basic python dependencies to be able to run tools/run_tests python scripts
# These dependencies are not sufficient to build gRPC Python, gRPC Python
# deps are defined elsewhere (e.g. python_deps.include)
RUN apt-get update && apt-get install -y \
  python3 \
  python3-pip \
  python3-setuptools \
  python3-yaml \
  && apt-get clean

# use pinned version of pip to avoid sudden breakages
RUN python3 -m pip install --upgrade pip==19.3.1

# TODO(jtattermusch): currently six is needed for tools/run_tests scripts
# but since our python2 usage is deprecated, we should get rid of it.
RUN python3 -m pip install six==1.16.0

# Google Cloud Platform API libraries
# These are needed for uploading test results to BigQuery (e.g. by tools/run_tests scripts)
RUN python3 -m pip install --upgrade google-auth==1.23.0 google-api-python-client==1.12.8 oauth2client==4.1.0



# Some bazel tests expect "python" binary to exist
RUN ln -s /usr/bin/python3 /usr/bin/python

# Install Google's LLVM build for Ubuntu 20.04
# Heavily inspired by https://github.com/GoogleCloudPlatform/container-definitions/tree/master/ubuntu1804_rbe
# See go/llvm-docker
# Note that we are using the google's pre-built tools for multiple reasons:
# * this is what the "official" rbe-ubuntu18-04 image is using: See https://github.com/GoogleCloudPlatform/layer-definitions/tree/master/layers/ubuntu1804/clang
# * for sanitizers to work, we need MSAN and TSAN enabled versions of libc++
# Check https://storage.googleapis.com/clang-builds-stable/clang-ubuntu20_04/latest.txt
# for the value of LLVM_LATEST.
ARG LLVM_LATEST="f2b94bd7eaa83d853dc7568fac87b1f8bf4ddec6"
RUN wget -q https://storage.googleapis.com/clang-builds-stable/clang-ubuntu20_04/clang_r${LLVM_LATEST}.tar.gz -O /tmp/clang.tar.gz \
  && tar -xzf /tmp/clang.tar.gz -C /usr/local && rm /tmp/clang.tar.gz
RUN wget -q https://storage.googleapis.com/clang-builds-stable/clang-ubuntu20_04/libcxx_r${LLVM_LATEST}.tar.gz -O /tmp/libcxx.tar.gz \
  && tar -xzf /tmp/libcxx.tar.gz -C /usr/local && rm /tmp/libcxx.tar.gz
RUN wget -q https://storage.googleapis.com/clang-builds-stable/clang-ubuntu20_04/libcxx-msan_r${LLVM_LATEST}.tar.gz -O /tmp/libcxx-msan.tar.gz \
  && mkdir -p /usr/local/libcxx-msan && tar -xzf /tmp/libcxx-msan.tar.gz -C /usr/local/libcxx-msan && rm /tmp/libcxx-msan.tar.gz
RUN wget -q https://storage.googleapis.com/clang-builds-stable/clang-ubuntu20_04/libcxx-tsan_r${LLVM_LATEST}.tar.gz -O /tmp/libcxx-tsan.tar.gz \
  && mkdir -p /usr/local/libcxx-tsan && tar -xzf /tmp/libcxx-tsan.tar.gz -C /usr/local/libcxx-tsan && rm /tmp/libcxx-tsan.tar.gz
RUN wget -q https://storage.googleapis.com/clang-builds-stable/clang-ubuntu20_04/lld_r${LLVM_LATEST}.tar.gz -O /tmp/lld.tar.gz \
  && tar -xzf /tmp/lld.tar.gz -C /usr/local && rm /tmp/lld.tar.gz
  
ENV ASAN_SYMBOLIZER_PATH=/usr/local/bin/llvm-symbolizer
ENV CC=/usr/local/bin/clang
ENV GCOV=/dev/null
ENV LD_LIBRARY_PATH=/usr/local/lib/x86_64-unknown-linux-gnu
ENV MSAN_SYMBOLIZER_PATH=/usr/local/bin/llvm-symbolizer
ENV TSAN_SYMBOLIZER_PATH=/usr/local/bin/llvm-symbolizer
ENV UBSAN_SYMBOLIZER_PATH=/usr/local/bin/llvm-symbolizer

# TODO(jtattermusch): Remove python2 once some tests stop failing without it.
RUN apt-get update && apt-get install -y python2 && apt-get clean

# Define the default command.
CMD ["bash"]
