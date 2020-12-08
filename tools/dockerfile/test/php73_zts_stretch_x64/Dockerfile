# Copyright 2016 gRPC authors.
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

FROM php:7.3-zts-stretch

RUN apt-get -qq update && apt-get -qq install -y \
  autoconf automake build-essential git libtool curl \
  zlib1g-dev \
  python-all-dev \
  python3-all-dev \
  python-setuptools

WORKDIR /tmp

RUN git clone https://github.com/krakjoe/pthreads

RUN cd pthreads && \
  phpize && \
  ./configure && \
  make && \
  make install

RUN curl https://bootstrap.pypa.io/get-pip.py | python2.7
RUN pip install --upgrade pip==19.3.1
RUN pip install virtualenv==16.7.9
RUN pip install futures==2.2.0 enum34==1.0.4 protobuf==3.5.2.post1 six==1.15.0 twisted==17.5.0

RUN curl -sS https://getcomposer.org/installer | php
RUN mv composer.phar /usr/local/bin/composer

RUN mkdir /var/local/jenkins

# Define the default command.
CMD ["bash"]
