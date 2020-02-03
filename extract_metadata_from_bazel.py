#!/usr/bin/env python
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

import subprocess
import yaml


def _get_bazel_deps(target_name):
    query = 'deps("%s")' % _get_bazel_label(target_name)
    output = subprocess.check_output(['tools/bazel', 'query', '--noimplicit_deps', '--output', 'label_kind', query])
    return output.splitlines()


def _extract_source_file_path(label):
    if label.startswith('source file //'):
        label = label[len('source file //'):]
    # labels in form //:src/core/lib/surface/call_test_only.h
    if label.startswith(':'):
        label = label[len(':'):]
    # labels in form //test/core/util:port.cc
    label = label.replace(':', '/')
    return label


def _get_bazel_label(target_name):
    if ':' in target_name:
        return '//%s' % target_name
    else:
        return '//:%s' % target_name


def _extract_public_headers(deps):
    result = []
    for dep in deps:
        if dep.startswith('source file //:include/') and dep.endswith('.h'):
            result.append(_extract_source_file_path(dep))
    return list(sorted(result))


def _extract_nonpublic_headers(deps):
    result = []
    for dep in deps:
        if dep.startswith('source file //') and not dep.startswith('source file //:include/') and dep.endswith('.h'):
            result.append(_extract_source_file_path(dep))
    return list(sorted(result))


def _extract_sources(deps):
    result = []
    for dep in deps:
        if dep.startswith('source file //') and (dep.endswith('.cc') or dep.endswith('.c') or dep.endswith('.proto')):
            result.append(_extract_source_file_path(dep))
    return list(sorted(result))


def _extract_cc_deps(deps):
    result = []
    for dep in deps:
        if dep.startswith('cc_library rule '):
            prefixlen = len('cc_library rule ')
            result.append(dep[prefixlen:])
    # TODO :remove itself from the deps?

    return list(sorted(result))


def _deps_sorted_reverse_topologically(lib_names, lib_dict):
    # TODO: actually implement this
    # expected output: if library A depends on B, A should be listed first
    result = list(sorted(lib_names))
    result.reverse()
    return result


def _get_target_metadata_from_bazel(lib_name):
    # extract the deps from bazel
    deps = _get_bazel_deps(lib_name)

    lib_dict = {}
    lib_dict['name'] = lib_name
    lib_dict['public_headers_transitive'] = _extract_public_headers(deps)
    lib_dict['headers_transitive'] = _extract_nonpublic_headers(deps)
    lib_dict['src_transitive'] = _extract_sources(deps)
    
    cc_deps = _extract_cc_deps(deps)
    
    lib_dict['deps_transitive'] = cc_deps
    depends_on_boringssl = True if filter(lambda dep : dep.startswith('@boringssl//'), cc_deps) else False

    # TODO: only set secure if uses boringssl directly, not transitively
    lib_dict['secure'] = depends_on_boringssl

    extra_props = _LIBS_EXTRA_PROPERTIES.get(lib_name, {})
    lib_dict.update(extra_props)

    return lib_dict

def _get_primitive_libs(lib_names):
    result = {}
    for lib_name in lib_names:
        result[lib_name] = _get_target_metadata_from_bazel(lib_name)

    # initialize the non-transitive fields
    for lib_name in lib_names:
        lib_dict = result[lib_name]
        lib_dict['public_headers'] = list(lib_dict['public_headers_transitive'])
        lib_dict['headers'] = list(lib_dict['headers_transitive'])
        lib_dict['src'] = list(lib_dict['src_transitive'])
        lib_dict['deps'] = []

    # pre-populate deps with external deps
    for lib_name in lib_names:
        lib_dict = result[lib_name]
        for dep_name in lib_dict['deps_transitive']:
            if dep_name.startswith('@com_google_absl//'):
                prefixlen = len('@com_google_absl//')
                # add the dependency on that library
                lib_dict['deps'] = lib_dict['deps'] + [dep_name[prefixlen:]]

    
    # postprocess transitive fields to get non-transitive values
    for lib_name in lib_names:
        lib_dict = result[lib_name]
        for dep_name in lib_names:
            dep_dict = result[dep_name]
            if lib_name != dep_name and filter(lambda x: x == _get_bazel_label(dep_name), lib_dict['deps_transitive']):
                src_difference = set(lib_dict['public_headers']).difference(set(dep_dict['public_headers_transitive']))
                lib_dict['public_headers'] = list(sorted(src_difference))
                src_difference = set(lib_dict['headers']).difference(set(dep_dict['headers_transitive']))
                lib_dict['headers'] = list(sorted(src_difference))
                src_difference = set(lib_dict['src']).difference(set(dep_dict['src_transitive']))
                lib_dict['src'] = list(sorted(src_difference))
                # add the dependency on that library
                lib_dict['deps'] = lib_dict['deps'] + [dep_name]

    # make sure deps are listed in reverse topological order (e.g. "grpc gpr" and not "gpr grpc")
    for lib_name in lib_names:
        lib_dict = result[lib_name]
        lib_dict['deps'] = _deps_sorted_reverse_topologically(lib_dict['deps'], result)

    # strip the transitive fields
    for lib_name in lib_names:
        lib_dict = result[lib_name]
        lib_dict.pop('public_headers_transitive', None)
        lib_dict.pop('headers_transitive', None)
        lib_dict.pop('src_transitive', None)
        lib_dict.pop('deps_transitive', None)

    # rename some targets to something else
    # TODO(jtattermusch): cleanup this code
    for lib_name in lib_names:
        to_name = _RENAME_TARGETS_DICT.get(lib_name, None)
        if to_name:
            lib_dict = result.pop(lib_name)
            lib_dict['name'] = to_name
            result[to_name] = lib_dict

            # update deps
            for lib_dict_to_update in result.values():
                lib_dict_to_update['deps'] = list(map(lambda dep: to_name if dep == lib_name else dep, lib_dict_to_update['deps']))

    return result


def _convert_to_build_yaml_like(lib_dict):
    lib_list = list(filter(lambda lib: lib.get('TYPE', 'library') == 'library' , lib_dict.values()))
    target_list = list(filter(lambda lib: lib.get('TYPE', 'library') == 'target' , lib_dict.values()))
    
    # get rid of the "TYPE" field
    for lib in lib_list:
        lib.pop('TYPE', None)
    for target in target_list:
        target.pop('TYPE', None)
        target.pop('public_headers', None)  # public headers make no sense for targets
    
    build_yaml_like = {
        'libs': lib_list,
        'filegroups': [],
        'targets': target_list,
        'tests': []
    }
    return build_yaml_like


# TODO: these are mostly "public libraries", in build.yaml they are as "build: all"
_PRIMITIVE_LIBS = [
    # TODO: translate name to address_sorting
    'third_party/address_sorting:address_sorting',  # technically doesn't belong here but it's defined like that in original build.yaml
    'gpr',
    'grpc',
    'grpc++',
    'grpc++_alts',
    'grpc++_error_details',
    'grpc++_reflection',
    'grpc++_unsecure',
    #'grpc_cronet' (no corresponding target in BUILD?)
    'grpc_csharp_ext',
    'grpc_unsecure',
    'grpcpp_channelz',


     # technically doesn't belong here
    'src/compiler:grpc_plugin_support', 
    'src/compiler:grpc_cpp_plugin',
    'src/compiler:grpc_csharp_plugin',
    'src/compiler:grpc_node_plugin',
    'src/compiler:grpc_objective_c_plugin',
    'src/compiler:grpc_php_plugin',
    'src/compiler:grpc_python_plugin',
    'src/compiler:grpc_ruby_plugin',
    
    #'grpc++_core_stats', TODO: is not build:all?
    #grpc++_proto_reflection_desc_db (no corresponding target in BUILD)
    ]

#dll: only, true, false:   manual?
_LIBS_EXTRA_PROPERTIES = {
    'third_party/address_sorting:address_sorting': { 'language': 'c', 'build': 'all' },
    'gpr': { 'language': 'c', 'build': 'all' },
    'grpc': { 'language': 'c', 'build': 'all', 'baselib': True, 'generate_plugin_registry': True},  # TODO: get list of plugins
    'grpc++': { 'language': 'c++', 'build': 'all', 'baselib': True },
    'grpc++_alts': { 'language': 'c++', 'build': 'all', 'baselib': True },
    'grpc++_error_details': { 'language': 'c++', 'build': 'all' },
    'grpc++_reflection': { 'language': 'c++', 'build': 'all' },
    'grpc++_unsecure': { 'language': 'c++', 'build': 'all', 'baselib': True },
    'grpc_csharp_ext': { 'language': 'c', 'build': 'all' },
    'grpc_unsecure': { 'language': 'c', 'build': 'all', 'baselib': True, 'generate_plugin_registry': True},  # TODO: get list of plugins
    'grpcpp_channelz': { 'language': 'c++', 'build': 'all' },

    'src/compiler:grpc_plugin_support': { 'language': 'c++', 'build': 'protoc' },
    'src/compiler:grpc_cpp_plugin': { 'language': 'c++', 'build': 'protoc', 'TYPE': 'target' },
    'src/compiler:grpc_csharp_plugin': { 'language': 'c++', 'build': 'protoc', 'TYPE': 'target' },
    'src/compiler:grpc_node_plugin': { 'language': 'c++', 'build': 'protoc', 'TYPE': 'target' },
    'src/compiler:grpc_objective_c_plugin': { 'language': 'c++', 'build': 'protoc', 'TYPE': 'target' },
    'src/compiler:grpc_php_plugin': { 'language': 'c++', 'build': 'protoc', 'TYPE': 'target' },
    'src/compiler:grpc_python_plugin': { 'language': 'c++', 'build': 'protoc', 'TYPE': 'target' },
    'src/compiler:grpc_ruby_plugin': { 'language': 'c++', 'build': 'protoc', 'TYPE': 'target' },
}

# rename the targets from the name used by bazel to format used historically by build.yaml
_RENAME_TARGETS_DICT ={
    'third_party/address_sorting:address_sorting': 'address_sorting',
    'src/compiler:grpc_plugin_support': 'grpc_plugin_support',
    'src/compiler:grpc_cpp_plugin': 'grpc_cpp_plugin',
    'src/compiler:grpc_csharp_plugin': 'grpc_csharp_plugin',
    'src/compiler:grpc_node_plugin': 'grpc_node_plugin',
    'src/compiler:grpc_objective_c_plugin': 'grpc_objective_c_plugin',
    'src/compiler:grpc_php_plugin': 'grpc_php_plugin',
    'src/compiler:grpc_python_plugin': 'grpc_python_plugin',
    'src/compiler:grpc_ruby_plugin': 'grpc_ruby_plugin',
}

# random selection of tests to verify that things build just fine
_TESTS = [
    # test helper libraries...
    'test/core/util:grpc_test_util',
    'test/cpp/util:test_config',
    'test/cpp/util:test_util',

    'test/core/avl:avl_test',
    'test/core/slice:slice_test',
    'test/core/security:credentials_test',
    #'test/cpp/interop:interop_test',  no good, uses binaries as data...
    'test/cpp/end2end:end2end_test',
]

# TODO: add libs for dependencies?
# @boringssl//
# @upb//
# @zlib//
# @com_google_absl
# @com_github_cares_cares//
# @com_google_protobuf//
# TODO: gtest

lib_dict = _get_primitive_libs(_PRIMITIVE_LIBS)
build_yaml_like = _convert_to_build_yaml_like(lib_dict)

with open('build_autogenerated.yaml', 'w') as file:
    documents = yaml.dump(build_yaml_like, file, default_flow_style=False)
