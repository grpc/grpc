#!/usr/bin/env python3
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

# produces cleaner build.yaml files

import collections
import os
import sys
import yaml

TEST = (os.environ.get('TEST', 'false') == 'true')

_TOP_LEVEL_KEYS = [
    'settings', 'proto_deps', 'filegroups', 'libs', 'targets', 'vspackages'
]
_ELEM_KEYS = [
    'name', 'gtest', 'cpu_cost', 'flaky', 'build', 'run', 'language',
    'public_headers', 'headers', 'src', 'deps'
]


def repr_ordered_dict(dumper, odict):
    return dumper.represent_mapping('tag:yaml.org,2002:map',
                                    list(odict.items()))


yaml.add_representer(collections.OrderedDict, repr_ordered_dict)


def _rebuild_as_ordered_dict(indict, special_keys):
    outdict = collections.OrderedDict()
    for key in sorted(indict.keys()):
        if '#' in key:
            outdict[key] = indict[key]
    for key in special_keys:
        if key in indict:
            outdict[key] = indict[key]
    for key in sorted(indict.keys()):
        if key in special_keys:
            continue
        if '#' in key:
            continue
        outdict[key] = indict[key]
    return outdict


def _clean_elem(indict):
    for name in ['public_headers', 'headers', 'src']:
        if name not in indict:
            continue
        inlist = indict[name]
        protos = list(x for x in inlist if os.path.splitext(x)[1] == '.proto')
        others = set(x for x in inlist if x not in protos)
        indict[name] = protos + sorted(others)
    return _rebuild_as_ordered_dict(indict, _ELEM_KEYS)


def cleaned_build_yaml_dict_as_string(indict):
    """Takes dictionary which represents yaml file and returns the cleaned-up yaml string"""
    js = _rebuild_as_ordered_dict(indict, _TOP_LEVEL_KEYS)
    for grp in ['filegroups', 'libs', 'targets']:
        if grp not in js:
            continue
        js[grp] = sorted([_clean_elem(x) for x in js[grp]],
                         key=lambda x: (x.get('language', '_'), x['name']))
    output = yaml.dump(js, indent=2, width=80, default_flow_style=False)
    # massage out trailing whitespace
    lines = []
    for line in output.splitlines():
        lines.append(line.rstrip() + '\n')
    output = ''.join(lines)
    return output


if __name__ == '__main__':
    for filename in sys.argv[1:]:
        with open(filename) as f:
            js = yaml.load(f, Loader=yaml.FullLoader)
        output = cleaned_build_yaml_dict_as_string(js)
        if TEST:
            with open(filename) as f:
                if not f.read() == output:
                    raise Exception(
                        'Looks like build-cleaner.py has not been run for file "%s"?'
                        % filename)
        else:
            with open(filename, 'w') as f:
                f.write(output)
