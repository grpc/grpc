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

FROM centos:7

RUN rpm --import "http://keyserver.ubuntu.com/pks/lookup?op=get&search=0x3FA7E0328081BFF6A14DA29AA6A19B38D3D831EF"
RUN yum-config-manager --add-repo http://download.mono-project.com/repo/centos/

RUN yum install -y mono

RUN yum install -y unzip

# --nogpgcheck because nuget-2.12 package is not signed.
RUN yum install -y nuget --nogpgcheck

# Help mono correctly locate libMonoPosixHelper.so
# as a workaround for issue https://bugzilla.xamarin.com/show_bug.cgi?id=42820
# The error message you'll get without this workaround:
# ```
# WARNING: /usr/lib/libMonoPosixHelper.so
# WARNING: Unable to read package from path 'Grpc.1.1.0-dev.nupkg'.
# ```
RUN cp /usr/lib64/libMonoPosixHelper.so /usr/lib/
