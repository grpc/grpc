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

FROM centos:7

RUN yum install -y python3
RUN yum install -y python3-devel
RUN yum install -y epel-release
RUN yum install -y python3-pip
RUN python3 -m pip install --upgrade pip==19.3.1
RUN python3 -m pip install -U virtualenv

# The default gcc of CentOS 7 is gcc 4.8 which is older than gcc 5.1,
# the minimum supported gcc version for gRPC Core so let's upgrade to
# the oldest one that can build gRPC on Centos 7.
RUN yum install -y centos-release-scl
RUN yum install -y devtoolset-8-binutils devtoolset-8-gcc devtoolset-8-gcc-c++

# Activate devtoolset-8 by default
# https://austindewey.com/2019/03/26/enabling-software-collections-binaries-on-a-docker-image/
RUN echo $'#!/bin/bash\n\
source scl_source enable devtoolset-8\n\
"$@"\n' > /usr/bin/entrypoint.sh
RUN chmod +x /usr/bin/entrypoint.sh
RUN cat /usr/bin/entrypoint.sh
ENTRYPOINT [ "/usr/bin/entrypoint.sh" ]

CMD ["/bin/bash"]
