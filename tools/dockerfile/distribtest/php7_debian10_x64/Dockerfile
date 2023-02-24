# Copyright 2020 gRPC authors.
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

FROM debian:10

RUN apt-get update && apt-get install -y php php-dev php-pear wget zlib1g-dev

RUN wget https://phar.phpunit.de/phpunit-8.5.8.phar && \
  mv phpunit-8.5.8.phar /usr/local/bin/phpunit && \
  chmod +x /usr/local/bin/phpunit
