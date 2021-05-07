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

# The aarch64 wheels are being crosscompiled to allow running the build
# on x64 machine. The dockcross/linux-armv7 image is a x86_64
# image with crosscompilation toolchain installed
FROM dockcross/linux-armv7

RUN apt update && apt install -y build-essential zlib1g-dev libncurses5-dev libgdbm-dev \
                                 libnss3-dev libssl-dev libreadline-dev libffi-dev && apt-get clean

ADD install_python_for_wheel_crosscompilation.sh /scripts/install_python_for_wheel_crosscompilation.sh

RUN /scripts/install_python_for_wheel_crosscompilation.sh 3.6.13 /opt/python/cp36-cp36m
RUN /scripts/install_python_for_wheel_crosscompilation.sh 3.7.10 /opt/python/cp37-cp37m
RUN /scripts/install_python_for_wheel_crosscompilation.sh 3.8.8 /opt/python/cp38-cp38
RUN /scripts/install_python_for_wheel_crosscompilation.sh 3.9.2 /opt/python/cp39-cp39

ENV AUDITWHEEL_ARCH armv7l
ENV AUDITWHEEL_PLAT linux_armv7l
