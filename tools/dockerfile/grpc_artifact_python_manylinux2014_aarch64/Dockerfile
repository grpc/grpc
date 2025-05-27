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

# AUTO-GENERATED FROM `$REPO_ROOT/templates/tools/dockerfile/grpc_artifact_python_manylinux2014_aarch64/Dockerfile.inja`!!!

FROM dockcross/manylinux2014-aarch64

# TODO: simplify the list of third_party modules list
# NOTE: git>=2.46 allows leading paths like third_party/* to include all subdirectories
# current docker base images use git versions lower than 2.46 and hence require separate configs for each submodule
RUN git config --global protocol.file.allow always && \
  git config --global --add safe.directory /var/local/jenkins/grpc && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/abseil-cpp && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/benchmark && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/bloaty && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/boringssl-with-bazel && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/cares/cares && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/envoy-api && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/googleapis && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/googletest && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/opencensus-proto && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/opentelemetry && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/opentelemetry-cpp && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/protobuf && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/protoc-gen-validate && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/re2 && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/xds && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git/modules/third_party/zlib

#=================
# Install ccache 

# Install ccache from source since ccache 3.x packaged with most linux distributions
# does not support Redis backend for caching.
RUN unset CMAKE_TOOLCHAIN_FILE && unset AS AR CC CPP CXX LD STRIP OBJCOPY \
    && curl -sSL -o ccache.tar.gz https://github.com/ccache/ccache/releases/download/v4.7.5/ccache-4.7.5.tar.gz \
    && tar -zxf ccache.tar.gz \
    && cd ccache-4.7.5 \
    && mkdir build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release -DZSTD_FROM_INTERNET=ON -DHIREDIS_FROM_INTERNET=ON .. \
    && make -j4 && make install \
    && cd ../.. \
    && rm -rf ccache-4.7.5 ccache.tar.gz

#===================================
# Install Python build requirements
RUN /opt/python/cp38-cp38/bin/pip install --upgrade 'cython==3.1.1' && \
  /opt/python/cp39-cp39/bin/pip install --upgrade 'cython==3.1.1' && \
  /opt/python/cp310-cp310/bin/pip install --upgrade 'cython==3.1.1' && \
  /opt/python/cp311-cp311/bin/pip install --upgrade 'cython==3.1.1' && \
  /opt/python/cp312-cp312/bin/pip install --upgrade 'cython==3.1.1' && \
  /opt/python/cp313-cp313/bin/pip install --upgrade 'cython==3.1.1'


# manylinux_2_17 is the preferred alias of manylinux2014
ENV AUDITWHEEL_PLAT manylinux_2_17_$AUDITWHEEL_ARCH
