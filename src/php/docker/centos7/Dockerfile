# Copyright 2019 gRPC authors.
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

FROM centos:centos7

RUN yum update -y && \
  yum install -y centos-release-scl && \
  yum install -y devtoolset-7-gcc*

SHELL [ "/usr/bin/scl", "enable", "devtoolset-7"]

RUN yum install epel-release -y && \
  rpm -Uvh https://mirror.webtatic.com/yum/el7/webtatic-release.rpm && \
  rpm -Uvh http://rpms.famillecollet.com/enterprise/remi-release-7.rpm && \
  yum --enablerepo=remi-php74 install php -y && \
  yum-config-manager --enable remi-php74 > /dev/null && \
  yum install -y make wget which \
    gmp-devel libmpc-devel mpfr-devel yum-utils \                                                            
    php-devel php-fpm php-pear && \                                                                          
  yum clean all -y  

ARG MAKEFLAGS=-j8


WORKDIR /tmp

RUN wget https://phar.phpunit.de/phpunit-8.5.13.phar && \
  mv phpunit-8.5.13.phar /usr/local/bin/phpunit && \
  chmod +x /usr/local/bin/phpunit


WORKDIR /github/grpc

COPY . .

RUN pear package && \
  find . -name grpc-*.tgz | xargs -I{} pecl install {}


CMD ["/github/grpc/src/php/bin/run_tests.sh", "--skip-persistent-channel-tests", \
     "--ignore-valgrind-undef-errors"]

