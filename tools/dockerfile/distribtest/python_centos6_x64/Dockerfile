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

FROM centos:6

RUN yum -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-6.noarch.rpm

# Vanilla CentOS6 only has python 2.6 and we don't support that.
RUN yum -y install yum -y install https://centos6.iuscommunity.org/ius-release.rpm
RUN yum install -y python27

# Override python2.6
RUN ln -s /usr/bin/python2.7 /usr/local/bin/python
RUN ln -s /usr/bin/python2.7 /usr/local/bin/python2

# Install pip
RUN curl https://bootstrap.pypa.io/get-pip.py | python -

# "which" command required by python's run_distrib_test.sh
RUN yum install -y which

RUN pip install virtualenv
