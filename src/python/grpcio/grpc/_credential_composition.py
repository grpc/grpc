# Copyright 2016 gRPC authors.
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

from grpc._cython import cygrpc


def _call(call_credentialses):
    call_credentials_iterator = iter(call_credentialses)
    composition = next(call_credentials_iterator)
    for additional_call_credentials in call_credentials_iterator:
        composition = cygrpc.call_credentials_composite(
            composition, additional_call_credentials)
    return composition


def call(call_credentialses):
    return _call(call_credentialses)


def channel(channel_credentials, call_credentialses):
    return cygrpc.channel_credentials_composite(channel_credentials,
                                                _call(call_credentialses))
