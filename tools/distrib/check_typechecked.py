#!/usr/bin/env python3

# Copyright 2022 gRPC authors.
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

from typeguard.importhook import TypeguardFinder
from typeguard.importhook import install_import_hook


class CustomFinder(TypeguardFinder):

    def should_instrument(self, module_name: str):
        # disregard the module names list and instrument all loaded modules
        should_evl = module_name.startswith("grpc._") and (
            not module_name.startswith("grpc._cython"))
        if should_evl:
            print("module_name: " + module_name)
            return True
        return False


install_import_hook('grpc', cls=CustomFinder)
import grpc

print(grpc._typing.__dict__)
