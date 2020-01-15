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


def seen_metadata(expected, actual):
    metadata_dict = dict(actual)
    if type(expected[0]) != tuple:
        return metadata_dict.get(expected[0]) == expected[1]
    else:
        for metadatum in expected:
            if metadata_dict.get(metadatum[0]) != metadatum[1]:
                return False
        return True
