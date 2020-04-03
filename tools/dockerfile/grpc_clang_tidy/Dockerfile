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

FROM debian:10

# Install clang-tidy 6.0
# This is because clang-tidy 7.0 started treating compiler errors as tidy errors
# and there are a couple of files which are not properly compiled via tidy so it
# should be using 6.0 version until all compilation errors are addressed.
RUN apt-get update && apt-get install -y clang-tidy-6.0
ENV CLANG_TIDY=clang-tidy-6.0

#====================
# Python dependencies

# Install dependencies

RUN apt-get update && apt-get install -y \
    python-all-dev \
    python3-all-dev \
    python-pip

# Install Python packages from PyPI
RUN pip install --upgrade pip==19.3.1
RUN pip install virtualenv==16.7.9
RUN pip install futures==2.2.0 enum34==1.0.4 protobuf==3.5.2.post1 six==1.10.0 twisted==17.5.0

ADD clang_tidy_all_the_things.sh /

# When running locally, we'll be impersonating the current user, so we need
# to make the script runnable by everyone.
RUN chmod a+rx /clang_tidy_all_the_things.sh

CMD ["echo 'Run with tools/distrib/clang_tidy_code.sh'"]
