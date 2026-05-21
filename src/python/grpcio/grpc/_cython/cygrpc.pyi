# Copyright 2026 The gRPC Authors
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

class ConnectivityState:
    idle: int
    connecting: int
    ready: int
    transient_failure: int
    shutdown: int

class StatusCode:
    ok: int
    cancelled: int
    unknown: int
    invalid_argument: int
    deadline_exceeded: int
    not_found: int
    already_exists: int
    permission_denied: int
    unauthenticated: int
    resource_exhausted: int
    failed_precondition: int
    aborted: int
    out_of_range: int
    unimplemented: int
    internal: int
    unavailable: int
    data_loss: int
