# Copyright 2024 The gRPC Authors
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

# AUTO-GENERATED FROM `$REPO_ROOT/templates/tools/dockerfile/grpc_artifact_python_musllinux_1_2_x64/Dockerfile.template`!!!

FROM quay.io/pypa/musllinux_1_2_x86_64:2025.07.25-1

RUN git config --global protocol.file.allow always && \
  git config --global --add safe.directory /var/local/jenkins/grpc && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git && \
  git config --global --add safe.directory '/var/local/jenkins/grpc/.git/modules/third_party/*'


# Preinstall zstd and hiredis for ccache to avoid *_FROM_INTERNET downloads.
RUN apk add openssl openssl-dev zstd zstd-dev hiredis-dev
#=================
# Install ccache

# Install ccache from source since ccache 3.x packaged with most linux distributions
# does not support Redis backend for caching.
RUN curl -sSL -o ccache.tar.gz https://github.com/ccache/ccache/releases/download/v4.11.3/ccache-4.11.3.tar.gz \
    && tar -zxf ccache.tar.gz \
    && cd ccache-4.11.3 \
    && mkdir build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release -DZSTD_FROM_INTERNET=ON -DHIREDIS_FROM_INTERNET=ON .. \
    && make -j4 && make install \
    && cd ../.. \
    && rm -rf ccache-4.11.3 ccache.tar.gz


#===================================
# Install Python build requirements
RUN /opt/python/cp39-cp39/bin/pip install --upgrade 'cython==3.1.1' && \
  /opt/python/cp310-cp310/bin/pip install --upgrade 'cython==3.1.1' && \
  /opt/python/cp311-cp311/bin/pip install --upgrade 'cython==3.1.1' && \
  /opt/python/cp312-cp312/bin/pip install --upgrade 'cython==3.1.1' && \
  /opt/python/cp313-cp313/bin/pip install --upgrade 'cython==3.1.1' && \
  /opt/python/cp314-cp314/bin/pip install --upgrade 'cython==3.1.1'

