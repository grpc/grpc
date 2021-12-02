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

FROM i386/debian:buster

RUN apt-get update && apt-get install -y python3 python3-pip

RUN python3 -m pip install virtualenv==16.7.9

RUN apt-get install -y build-essential
RUN apt-get install -y python3-dev

# docker is running on a 64-bit machine, so we need to
# override "uname -m" to report i686 instead of x86_64, otherwise
# python will choose a wrong binary package to install.
ENTRYPOINT ["linux32"]
