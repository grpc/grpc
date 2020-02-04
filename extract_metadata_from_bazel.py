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


def _get_bazel_label(target_name):
    if ':' in target_name:
        return '//%s' % target_name
    else:
        return '//:%s' % target_name


def _get_bazel_deps(target_name):
    """Get transitive dependencies from bazel by running 'bazel query deps()'"""
    query = 'deps("%s")' % _get_bazel_label(target_name)
    output = subprocess.check_output(['tools/bazel', 'query', '--noimplicit_deps', '--output', 'label_kind', query])
    return output.splitlines()


def _extract_source_file_path(label):
    """Gets relative path to source file from bazel deps listing"""
    if label.startswith('source file //'):
        label = label[len('source file //'):]
    # labels in form //:src/core/lib/surface/call_test_only.h
    if label.startswith(':'):
        label = label[len(':'):]
    # labels in form //test/core/util:port.cc
    label = label.replace(':', '/')
    return label


def _extract_public_headers(deps):
    """Gets list of public headers from bazel deps listing"""
    result = []
    for dep in deps:
        if dep.startswith('source file //:include/') and dep.endswith('.h'):
            result.append(_extract_source_file_path(dep))
    return list(sorted(result))


def _extract_nonpublic_headers(deps):
    """Gets list of non-public headers from bazel deps listing"""
    result = []
    for dep in deps:
        if dep.startswith('source file //') and not dep.startswith('source file //:include/') and dep.endswith('.h'):
            result.append(_extract_source_file_path(dep))
    return list(sorted(result))


def _extract_sources(deps):
    """Gets list of source files from bazel deps listing"""
    result = []
    for dep in deps:
        if dep.startswith('source file //') and (dep.endswith('.cc') or dep.endswith('.c') or dep.endswith('.proto')):
            result.append(_extract_source_file_path(dep))
    return list(sorted(result))


def _extract_cc_deps(deps):
    """Gets list of cc_library dependencies from bazel deps listing"""
    result = []
    for dep in deps:
        if dep.startswith('cc_library rule '):
            prefixlen = len('cc_library rule ')
            result.append(dep[prefixlen:])
    return list(sorted(result))


def _sort_by_build_order(lib_names, lib_dict):
    """Sort library names to form correct build order. Use metadata from lib_dict"""
    # we find correct build order by performing a topological sort
    # expected output: if library B depends on A, A should be listed first
    
    # all libs that are not in the dictionary are considered external.
    external_deps = list(sorted(filter(lambda lib_name: lib_name not in lib_dict, lib_names)))

    result = list(external_deps)  # external deps will be listed first
    while len(result) < len(lib_names):
        more_results = []
        for lib in lib_names:
            if lib not in result:
                dep_set = set(lib_dict[lib]['deps']).intersection(lib_names)
                if not dep_set.difference(set(result)):
                    more_results.append(lib)
        if not more_results:
            raise Exception('Cannot sort topologically, there seems to be a cyclic dependency')
        result = result + list(sorted(more_results))  # when build order doesn't matter, sort lexicographically
    return result


def _get_target_metadata_from_bazel(target_name):
    # extract the deps from bazel
    deps = _get_bazel_deps(target_name)

    lib_dict = {}
    lib_dict['name'] = target_name
    lib_dict['public_headers_transitive'] = _extract_public_headers(deps)
    lib_dict['headers_transitive'] = _extract_nonpublic_headers(deps)
    lib_dict['src_transitive'] = _extract_sources(deps)
    
    cc_deps = _extract_cc_deps(deps)
    
    lib_dict['deps_transitive'] = cc_deps
    depends_on_boringssl = True if filter(lambda dep : dep.startswith('@boringssl//'), cc_deps) else False

    # TODO: only set secure if uses boringssl directly, not transitively
    lib_dict['secure'] = depends_on_boringssl

    return lib_dict

def _get_primitive_libs(lib_names):
    result = {}
    for lib_name in lib_names:
        result[lib_name] = _get_target_metadata_from_bazel(lib_name)

    # populate extra properties from build metadata
    for lib_name in lib_names:
        result[lib_name].update(_BUILD_METADATA.get(lib_name, {}))

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
        to_name = _BUILD_METADATA.get(lib_name, {}).get('_RENAME', None)
        if to_name:
            # TODO: make sure the name doesn't exist
            if to_name in result:
                raise Exception('Cannot rename target ' + lib_name + ', ' + to_name + ' already exists.')
            lib_dict = result.pop(lib_name)
            lib_dict['name'] = to_name
            result[to_name] = lib_dict

            # update deps
            for lib_dict_to_update in result.values():
                lib_dict_to_update['deps'] = list(map(lambda dep: to_name if dep == lib_name else dep, lib_dict_to_update['deps']))

    # make sure deps are listed in reverse topological order (e.g. "grpc gpr" and not "gpr grpc")
    for lib_dict in result.itervalues():
        lib_dict['deps'] = list(reversed(_sort_by_build_order(lib_dict['deps'], result)))

    return result


def _convert_to_build_yaml_like(lib_dict):
    lib_names = list(filter(lambda lib_name: lib_dict[lib_name].get('_TYPE', 'library') == 'library' , lib_dict.keys()))
    target_names = list(filter(lambda lib_name: lib_dict[lib_name].get('_TYPE', 'library') == 'target' , lib_dict.keys()))
    test_names = list(filter(lambda lib_name: lib_dict[lib_name].get('_TYPE', 'library') == 'test' , lib_dict.keys()))

    # make sure libraries come in build order (seems to be required by Makefile)
    lib_names = _sort_by_build_order(lib_names, lib_dict)
    target_names = _sort_by_build_order(target_names, lib_dict)
    test_names = _sort_by_build_order(test_names, lib_dict)

    # list libraries and targets in predefined order
    lib_list = list(map(lambda lib_name: lib_dict[lib_name], lib_names))
    target_list = list(map(lambda lib_name: lib_dict[lib_name], target_names))
    test_list = list(map(lambda lib_name: lib_dict[lib_name], test_names))
    
    # get rid of the "TYPE" field
    for lib in lib_list:
        lib.pop('_TYPE', None)
        lib.pop('_RENAME', None)
    for target in target_list:
        target.pop('_TYPE', None)
        target.pop('_RENAME', None)
        target.pop('public_headers', None)  # public headers make no sense for targets
    for test in test_list:
        test.pop('_TYPE', None)
        test.pop('_RENAME', None)
        test.pop('public_headers', None)  # public headers make no sense for tests
    
    build_yaml_like = {
        'libs': lib_list,
        'filegroups': [],
        'targets': target_list,
        'tests': test_list,
    }
    return build_yaml_like


# extra metadata that will be used to construct build.yaml
# there are mostly extra properties that we weren't able to obtain from the bazel build
# _TYPE: whether this is library, target or test
# _RENAME: whether this target should be renamed to a different name (to match expectations of make and cmake builds)
# TODO: dll: only, true, false: set manually?
_BUILD_METADATA = {
    'third_party/address_sorting:address_sorting': { 'language': 'c', 'build': 'all', '_RENAME': 'address_sorting' },
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
    #'grpc_cronet' (no corresponding target in BUILD?)

    'src/compiler:grpc_plugin_support': { 'language': 'c++', 'build': 'protoc', '_RENAME': 'grpc_plugin_support' },
    'src/compiler:grpc_cpp_plugin': { 'language': 'c++', 'build': 'protoc', '_TYPE': 'target', '_RENAME': 'grpc_cpp_plugin' },
    'src/compiler:grpc_csharp_plugin': { 'language': 'c++', 'build': 'protoc', '_TYPE': 'target', '_RENAME': 'grpc_csharp_plugin' },
    'src/compiler:grpc_node_plugin': { 'language': 'c++', 'build': 'protoc', '_TYPE': 'target', '_RENAME': 'grpc_node_plugin' },
    'src/compiler:grpc_objective_c_plugin': { 'language': 'c++', 'build': 'protoc', '_TYPE': 'target', '_RENAME': 'grpc_objective_c_plugin' },
    'src/compiler:grpc_php_plugin': { 'language': 'c++', 'build': 'protoc', '_TYPE': 'target', '_RENAME': 'grpc_php_plugin' },
    'src/compiler:grpc_python_plugin': { 'language': 'c++', 'build': 'protoc', '_TYPE': 'target', '_RENAME': 'grpc_python_plugin' },
    'src/compiler:grpc_ruby_plugin': { 'language': 'c++', 'build': 'protoc', '_TYPE': 'target', '_RENAME': 'grpc_ruby_plugin' },

    #'grpc++_core_stats', TODO: is not build:all?
    #grpc++_proto_reflection_desc_db (no corresponding target in BUILD)

    'test/core/util:grpc_test_util': { 'language': 'c', 'build': 'private', '_RENAME': 'grpc_test_util' },
    'test/core/avl:avl_test': { 'language': 'c', 'build': 'test', '_TYPE': 'target', '_RENAME': 'avl_test' },
    'test/core/slice:slice_test': { 'language': 'c', 'build': 'test', '_TYPE': 'target', '_RENAME': 'slice_test' },

    'test/cpp/util:test_config': { 'language': 'c++', 'build': 'private', '_RENAME': 'grpc++_test_config' },
    'test/cpp/util:test_util': { 'language': 'c++', 'build': 'private', '_RENAME': 'grpc++_test_util' },
    'test/cpp/end2end:end2end_test': { 'language': 'c++', 'build': 'test', 'gtest': True, '_TYPE': 'target', '_RENAME': 'end2end_test' },
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

lib_dict = _get_primitive_libs(_BUILD_METADATA.keys())
build_yaml_like = _convert_to_build_yaml_like(lib_dict)

with open('build_autogenerated.yaml', 'w') as file:
    documents = yaml.dump(build_yaml_like, file, default_flow_style=False)
