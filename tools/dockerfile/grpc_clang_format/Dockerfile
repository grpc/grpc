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

FROM debian:jessie

RUN apt-get update && apt-get -y install wget xz-utils
RUN wget http://releases.llvm.org/5.0.0/clang+llvm-5.0.0-linux-x86_64-ubuntu14.04.tar.xz
RUN tar xf clang+llvm-5.0.0-linux-x86_64-ubuntu14.04.tar.xz
RUN ln -s /clang+llvm-5.0.0-linux-x86_64-ubuntu14.04/bin/clang-format /usr/local/bin/clang-format
ENV CLANG_FORMAT=clang-format
RUN ln -s /clang+llvm-5.0.0-linux-x86_64-ubuntu14.04/bin/clang-tidy /usr/local/bin/clang-tidy
ENV CLANG_TIDY=clang-tidy

ADD clang_format_all_the_things.sh /

# When running locally, we'll be impersonating the current user, so we need
# to make the script runnable by everyone.
RUN chmod a+rx /clang_format_all_the_things.sh

CMD ["echo 'Run with tools/distrib/clang_format_code.sh'"]
