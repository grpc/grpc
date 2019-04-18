# Copyright 2018 gRPC authors.
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
"""Buldigen generate grpc_shadow_boringssl headers
This script takes the list of symbols from
src/objective-c/grpc_shadow_boringssl_symbols and populate them in
settings.grpc_shadow_boringssl_symbols
"""


def mako_plugin(dictionary):
    with open('src/objective-c/grpc_shadow_boringssl_symbol_list') as f:
        symbols = f.readlines()
    # Remove trailing '\n'
    symbols = [s.strip() for s in symbols]
    # Remove comments
    symbols = [s for s in symbols if s[0] != '#']
    # Remove the commit number
    del symbols[0]

    settings = dictionary['settings']
    settings['grpc_shadow_boringssl_symbols'] = symbols
