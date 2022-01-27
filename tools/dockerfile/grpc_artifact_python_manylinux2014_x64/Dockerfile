# Copyright 2020 The gRPC Authors
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

# Docker file for building gRPC manylinux Python artifacts.

FROM quay.io/pypa/manylinux2014_x86_64:2021-08-26-12e5da0

# manylinux_2_17 is the preferred alias of manylinux2014
ENV AUDITWHEEL_PLAT manylinux_2_17_$AUDITWHEEL_ARCH

# TODO(jtattermusch): revisit which of the deps are really required
RUN yum update -y && yum install -y curl-devel expat-devel gettext-devel openssl-devel zlib-devel

###################################
# Install Python build requirements
RUN /opt/python/cp36-cp36m/bin/pip install --upgrade cython
RUN /opt/python/cp37-cp37m/bin/pip install --upgrade cython
RUN /opt/python/cp38-cp38/bin/pip install --upgrade cython
RUN /opt/python/cp39-cp39/bin/pip install --upgrade cython
RUN /opt/python/cp310-cp310/bin/pip install --upgrade cython
