# Copyright 2018 The gRPC Authors
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

FROM alpine:3.11

RUN apk add --update build-base linux-headers python python2-dev py2-pip

RUN pip install --upgrade pip==19.3.1

RUN pip install virtualenv

# bash is required for our test script invocation
# ideally, we want to fix the invocation mechanism
# so we can remove this, but it has to be here for
# now:
RUN apk add --update bash
