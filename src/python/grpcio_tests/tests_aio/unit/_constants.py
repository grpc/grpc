# Copyright 2020 The gRPC Authors
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

# If we use an unreachable IP, depending on the network stack, we might not get
# with an RST fast enough. This used to cause tests to flake under different
# platforms.
UNREACHABLE_TARGET = "foo/bar"
UNARY_CALL_WITH_SLEEP_VALUE = 0.2
