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
"""Allows dot-accessible dictionaries."""


class Bunch(dict):

    def __init__(self, d):
        dict.__init__(self, d)
        self.__dict__.update(d)


# Converts any kind of variable to a Bunch
def to_bunch(var):
    if isinstance(var, list):
        return [to_bunch(i) for i in var]
    if isinstance(var, dict):
        ret = {}
        for k, v in var.items():
            if isinstance(v, (list, dict)):
                v = to_bunch(v)
            ret[k] = v
        return Bunch(ret)
    else:
        return var


# Merges JSON 'add' into JSON 'dst'
def merge_json(dst, add):
    if isinstance(dst, dict) and isinstance(add, dict):
        for k, v in add.items():
            if k in dst:
                if k == '#': continue
                merge_json(dst[k], v)
            else:
                dst[k] = v
    elif isinstance(dst, list) and isinstance(add, list):
        dst.extend(add)
    else:
        raise Exception(
            'Tried to merge incompatible objects %s %s\n\n%r\n\n%r' %
            (type(dst).__name__, type(add).__name__, dst, add))
