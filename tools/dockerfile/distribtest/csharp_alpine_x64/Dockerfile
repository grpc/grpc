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

FROM microsoft/dotnet:2.1-sdk-alpine

RUN apk update && apk add bash
RUN apk update && apk add unzip

# needed to satisfy the "ld-linux-x86-64.so.2" dependency
RUN apk add libc6-compat

#RUN apk add libnsl
#RUN cp /usr/lib/libnsl.so.2 /usr/lib/libnsl.so.1
