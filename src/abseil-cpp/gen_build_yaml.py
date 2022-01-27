#!/usr/bin/env python2.7

# Copyright 2019 gRPC authors.
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

import os
import yaml

BUILDS_YAML_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                'preprocessed_builds.yaml')
with open(BUILDS_YAML_PATH) as f:
    builds = yaml.load(f, Loader=yaml.FullLoader)

for build in builds:
    build['build'] = 'private'
    build['build_system'] = []
    build['language'] = 'c'
    build['secure'] = False
print(yaml.dump({'libs': builds}))
