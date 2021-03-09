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

FROM debian:jessie


#=================
# PHP7 dependencies

# Install Git and basic packages.
RUN apt-get update && apt-get install -y \
  autoconf \
  automake \
  build-essential \
  ccache \
  curl \
  git \
  libcurl4-openssl-dev \
  libgmp-dev \
  libgmp3-dev \
  libssl-dev \
  libtool \
  libxml2-dev \
  pkg-config \
  re2c \
  time \
  unzip \
  wget \
  zip && apt-get clean

# Install other dependencies
RUN ln -sf /usr/include/x86_64-linux-gnu/gmp.h /usr/include/gmp.h
RUN wget http://ftp.gnu.org/gnu/bison/bison-3.4.2.tar.gz -O /var/local/bison-3.4.2.tar.gz
RUN cd /var/local \
  && tar -zxvf bison-3.4.2.tar.gz \
  && cd /var/local/bison-3.4.2 \
  && ./configure \
  && make \
  && make install

# Compile PHP7 from source
RUN git clone https://github.com/php/php-src /var/local/git/php-src
RUN cd /var/local/git/php-src \
  && git checkout PHP-7.2.22 \
  && ./buildconf --force \
  && ./configure \
  --with-gmp \
  --with-openssl \
  --with-zlib \
  && make \
  && make install

#====================
# Python dependencies

# Install dependencies

RUN apt-get update && apt-get install -y \
    python-all-dev \
    python3-all-dev \
    python-setuptools

# Install Python packages from PyPI
RUN curl https://bootstrap.pypa.io/pip/2.7/get-pip.py | python2.7
RUN pip install --upgrade pip==19.3.1
RUN pip install virtualenv==16.7.9
RUN pip install futures==2.2.0 enum34==1.0.4 protobuf==3.5.2.post1 six==1.15.0 twisted==17.5.0

# Google Cloud platform API libraries
RUN pip install --upgrade google-auth==1.24.0 google-api-python-client==1.12.8 oauth2client==4.1.0

#=================	
# PHP Test dependencies	

 RUN apt-get update && apt-get install -y \	
    valgrind


RUN mkdir /var/local/jenkins

# Define the default command.
CMD ["bash"]
