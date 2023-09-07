# Copyright 2023 gRPC authors.
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
import framework.infrastructure.traffic_director as td_base


# TODO(sergiitk): [GAMMA] make a TD-manager-less base test case.
class TrafficDirectorGammaManager(td_base.TrafficDirectorManager):
    """Gamma."""

    def cleanup(self, *, force=False):  # pylint: disable=unused-argument
        return True
