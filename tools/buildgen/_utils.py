#!/usr/bin/env python3
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
"""Utility functions for build file generation scripts."""

import os
import sys
import types
import importlib.util
from typing import Any, Union, Mapping, List


def import_python_module(path: str) -> types.ModuleType:
    """Imports the Python file at the given path, returns a module object."""
    module_name = os.path.basename(path).replace('.py', '')
    spec = importlib.util.spec_from_file_location(module_name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


class Bunch(dict):
    """Allows dot-accessible dictionaries."""

    def __init__(self, d: Mapping):
        dict.__init__(self, d)
        self.__dict__.update(d)


def to_bunch(var: Any) -> Any:
    """Converts any kind of variable to a Bunch."""
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


def merge_json(dst: Union[Mapping, List], add: Union[Mapping, List]) -> None:
    """Merges JSON objects recursively."""
    if isinstance(dst, dict) and isinstance(add, dict):
        for k, v in add.items():
            if k in dst:
                if k.startswith('#'):
                    continue
                merge_json(dst[k], v)
            else:
                dst[k] = v
    elif isinstance(dst, list) and isinstance(add, list):
        dst.extend(add)
    else:
        raise TypeError(
            'Tried to merge incompatible objects %s %s\n\n%r\n\n%r' %
            (type(dst).__name__, type(add).__name__, dst, add))
