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
import xml.etree.ElementTree as ET
import os
import sys
import build_cleaner

_ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(_ROOT)


def _bazel_query_xml_tree(query):
    """Get xml output of bazel query invocation, parsed as XML tree"""
    output = subprocess.check_output(
        ['tools/bazel', 'query', '--noimplicit_deps', '--output', 'xml', query])
    return ET.fromstring(output)


def _rule_dict_from_xml_node(rule_xml_node):
    result = {
        'class': rule_xml_node.attrib.get('class'),
        'name': rule_xml_node.attrib.get('name'),
        'srcs': [],
        'hdrs': [],
        'deps': [],
        'data': [],
        'tags': [],
        'args': [],
        'generator_function': None,
        'size': None,
    }
    for child in rule_xml_node:
        # all the metadata we want is stored under "list" tags
        if child.tag == 'list':
            list_name = child.attrib['name']
            if list_name in ['srcs', 'hdrs', 'deps', 'data', 'tags', 'args']:
                result[list_name] += [item.attrib['value'] for item in child]
        if child.tag == 'string':
            string_name = child.attrib['name']
            if string_name in ['generator_function', 'size']:
                result[string_name] = child.attrib['value']
    return result


def _extract_rules_from_bazel_xml(xml_tree):
    result = {}
    for child in xml_tree:
        if child.tag == 'rule':
            rule_dict = _rule_dict_from_xml_node(child)
            rule_clazz = rule_dict['class']
            rule_name = rule_dict['name']
            if rule_clazz in [
                    'cc_library', 'cc_binary', 'cc_test', 'cc_proto_library',
                    'proto_library'
            ]:
                if rule_name in result:
                    raise Exception('Rule %s already present' % rule_name)
                result[rule_name] = rule_dict
    return result


def _get_bazel_label(target_name):
    if ':' in target_name:
        return '//%s' % target_name
    else:
        return '//:%s' % target_name


def _extract_source_file_path(label):
    """Gets relative path to source file from bazel deps listing"""
    if label.startswith('//'):
        label = label[len('//'):]
    # labels in form //:src/core/lib/surface/call_test_only.h
    if label.startswith(':'):
        label = label[len(':'):]
    # labels in form //test/core/util:port.cc
    label = label.replace(':', '/')
    return label


def _extract_public_headers(bazel_rule):
    """Gets list of public headers from a bazel rule"""
    result = []
    for dep in bazel_rule['hdrs']:
        if dep.startswith('//:include/') and dep.endswith('.h'):
            result.append(_extract_source_file_path(dep))
    return list(sorted(result))


def _extract_nonpublic_headers(bazel_rule):
    """Gets list of non-public headers from a bazel rule"""
    result = []
    for dep in bazel_rule['hdrs']:
        if dep.startswith('//') and not dep.startswith(
                '//:include/') and dep.endswith('.h'):
            result.append(_extract_source_file_path(dep))
    return list(sorted(result))


def _extract_sources(bazel_rule):
    """Gets list of source files from a bazel rule"""
    result = []
    for dep in bazel_rule['srcs']:
        if dep.startswith('//') and (dep.endswith('.cc') or dep.endswith('.c')
                                     or dep.endswith('.proto')):
            result.append(_extract_source_file_path(dep))
    return list(sorted(result))


def _extract_deps(bazel_rule):
    """Gets list of deps from from a bazel rule"""
    return list(sorted(bazel_rule['deps']))


def _create_target_from_bazel_rule(target_name, bazel_rules):
    # extract the deps from bazel
    bazel_rule = bazel_rules[_get_bazel_label(target_name)]
    result = {
        'name': target_name,
        '_PUBLIC_HEADERS_BAZEL': _extract_public_headers(bazel_rule),
        '_HEADERS_BAZEL': _extract_nonpublic_headers(bazel_rule),
        '_SRC_BAZEL': _extract_sources(bazel_rule),
        '_DEPS_BAZEL': _extract_deps(bazel_rule),
    }
    return result


def _sort_by_build_order(lib_names, lib_dict, deps_key_name, verbose=False):
    """Sort library names to form correct build order. Use metadata from lib_dict"""
    # we find correct build order by performing a topological sort
    # expected output: if library B depends on A, A should be listed first

    # all libs that are not in the dictionary are considered external.
    external_deps = list(
        sorted(filter(lambda lib_name: lib_name not in lib_dict, lib_names)))
    if verbose:
        print('topo_ordering ' + str(lib_names))
        print('    external_deps ' + str(external_deps))

    result = list(external_deps)  # external deps will be listed first
    while len(result) < len(lib_names):
        more_results = []
        for lib in lib_names:
            if lib not in result:
                dep_set = set(lib_dict[lib].get(deps_key_name, []))
                dep_set = dep_set.intersection(lib_names)
                # if lib only depends on what's already built, add it to the results
                if not dep_set.difference(set(result)):
                    more_results.append(lib)
        if not more_results:
            raise Exception(
                'Cannot sort topologically, there seems to be a cyclic dependency'
            )
        if verbose:
            print('    adding ' + str(more_results))
        result = result + list(
            sorted(more_results
                  ))  # when build order doesn't matter, sort lexicographically
    return result


# TODO(jtattermusch): deduplicate with transitive_dependencies.py (which has a slightly different logic)
def _populate_transitive_deps(bazel_rules):
    """Add 'transitive_deps' field for each of the rules"""
    transitive_deps = {}
    for rule_name in bazel_rules.iterkeys():
        transitive_deps[rule_name] = set(bazel_rules[rule_name]['deps'])

    while True:
        deps_added = 0
        for rule_name in bazel_rules.iterkeys():
            old_deps = transitive_deps[rule_name]
            new_deps = set(old_deps)
            for dep_name in old_deps:
                new_deps.update(transitive_deps.get(dep_name, set()))
            deps_added += len(new_deps) - len(old_deps)
            transitive_deps[rule_name] = new_deps
        # if none of the transitive dep sets has changed, we're done
        if deps_added == 0:
            break

    for rule_name, bazel_rule in bazel_rules.iteritems():
        bazel_rule['transitive_deps'] = list(sorted(transitive_deps[rule_name]))


def _external_dep_name_from_bazel_dependency(bazel_dep):
    """Returns name of dependency if external bazel dependency is provided or None"""
    if bazel_dep.startswith('@com_google_absl//'):
        # special case for add dependency on one of the absl libraries (there is not just one absl library)
        prefixlen = len('@com_google_absl//')
        return bazel_dep[prefixlen:]
    elif bazel_dep == '//external:upb_lib':
        return 'upb'
    elif bazel_dep == '//external:benchmark':
        return 'benchmark'
    else:
        # all the other external deps such as gflags, protobuf, cares, zlib
        # don't need to be listed explicitly, they are handled automatically
        # by the build system (make, cmake)
        return None


def _expand_intermediate_deps(target_dict, public_dep_names, bazel_rules):
    # Some of the libraries defined by bazel won't be exposed in build.yaml
    # We call these "intermediate" dependencies. This method expands
    # the intermediate deps for given target (populates library's
    # headers, sources and dicts as if the intermediate dependency never existed)

    # use this dictionary to translate from bazel labels to dep names
    bazel_label_to_dep_name = {}
    for dep_name in public_dep_names:
        bazel_label_to_dep_name[_get_bazel_label(dep_name)] = dep_name

    target_name = target_dict['name']
    bazel_deps = target_dict['_DEPS_BAZEL']

    # initial values
    public_headers = set(target_dict['_PUBLIC_HEADERS_BAZEL'])
    headers = set(target_dict['_HEADERS_BAZEL'])
    src = set(target_dict['_SRC_BAZEL'])
    deps = set()

    expansion_blacklist = set()
    to_expand = set(bazel_deps)
    while to_expand:

        # start with the last dependency to be built
        build_order = _sort_by_build_order(list(to_expand), bazel_rules,
                                           'transitive_deps')

        bazel_dep = build_order[-1]
        to_expand.remove(bazel_dep)

        is_public = bazel_dep in bazel_label_to_dep_name
        external_dep_name_maybe = _external_dep_name_from_bazel_dependency(
            bazel_dep)

        if is_public:
            # this is not an intermediate dependency we so we add it
            # to the list of public dependencies to the list, in the right format
            deps.add(bazel_label_to_dep_name[bazel_dep])

            # we do not want to expand any intermediate libraries that are already included
            # by the dependency we just added
            expansion_blacklist.update(
                bazel_rules[bazel_dep]['transitive_deps'])

        elif external_dep_name_maybe:
            deps.add(external_dep_name_maybe)

        elif bazel_dep.startswith(
                '//external:') or not bazel_dep.startswith('//'):
            # all the other external deps can be skipped
            pass

        elif bazel_dep in expansion_blacklist:
            # do not expand if a public dependency that depends on this has already been expanded
            pass

        else:
            if bazel_dep in bazel_rules:
                # this is an intermediate library, expand it
                public_headers.update(
                    _extract_public_headers(bazel_rules[bazel_dep]))
                headers.update(
                    _extract_nonpublic_headers(bazel_rules[bazel_dep]))
                src.update(_extract_sources(bazel_rules[bazel_dep]))

                new_deps = _extract_deps(bazel_rules[bazel_dep])
                to_expand.update(new_deps)
            else:
                raise Exception(bazel_dep + ' not in bazel_rules')

    # make the 'deps' field transitive, but only list non-intermediate deps and selected external deps
    bazel_transitive_deps = bazel_rules[_get_bazel_label(
        target_name)]['transitive_deps']
    for transitive_bazel_dep in bazel_transitive_deps:
        public_name = bazel_label_to_dep_name.get(transitive_bazel_dep, None)
        if public_name:
            deps.add(public_name)
        external_dep_name_maybe = _external_dep_name_from_bazel_dependency(
            transitive_bazel_dep)
        if external_dep_name_maybe:
            # expanding all absl libraries is technically correct but creates too much noise
            if not external_dep_name_maybe.startswith('absl'):
                deps.add(external_dep_name_maybe)

    target_dict['public_headers'] = list(sorted(public_headers))
    target_dict['headers'] = list(sorted(headers))
    target_dict['src'] = list(sorted(src))
    target_dict['deps'] = list(sorted(deps))


def _generate_build_metadata(build_extra_metadata, bazel_rules):
    lib_names = build_extra_metadata.keys()
    result = {}

    for lib_name in lib_names:
        lib_dict = _create_target_from_bazel_rule(lib_name, bazel_rules)

        _expand_intermediate_deps(lib_dict, lib_names, bazel_rules)

        # populate extra properties from build metadata
        lib_dict.update(build_extra_metadata.get(lib_name, {}))

        # store to results
        result[lib_name] = lib_dict

    # rename some targets to something else
    # this needs to be made after we're done with most of processing logic
    # otherwise the already-renamed libraries will have different names than expected
    for lib_name in lib_names:
        to_name = build_extra_metadata.get(lib_name, {}).get('_RENAME', None)
        if to_name:
            # store lib under the new name and also change its 'name' property
            if to_name in result:
                raise Exception('Cannot rename target ' + lib_name + ', ' +
                                to_name + ' already exists.')
            lib_dict = result.pop(lib_name)
            lib_dict['name'] = to_name
            result[to_name] = lib_dict

            # dep names need to be updated as well
            for lib_dict_to_update in result.values():
                lib_dict_to_update['deps'] = list(
                    map(lambda dep: to_name if dep == lib_name else dep,
                        lib_dict_to_update['deps']))

    # make sure deps are listed in reverse topological order (e.g. "grpc gpr" and not "gpr grpc")
    for lib_dict in result.itervalues():
        lib_dict['deps'] = list(
            reversed(_sort_by_build_order(lib_dict['deps'], result, 'deps')))

    return result


def _convert_to_build_yaml_like(lib_dict):
    lib_names = list(
        filter(
            lambda lib_name: lib_dict[lib_name].get('_TYPE', 'library') ==
            'library', lib_dict.keys()))
    target_names = list(
        filter(
            lambda lib_name: lib_dict[lib_name].get('_TYPE', 'library') ==
            'target', lib_dict.keys()))
    test_names = list(
        filter(
            lambda lib_name: lib_dict[lib_name].get('_TYPE', 'library') ==
            'test', lib_dict.keys()))

    # list libraries and targets in predefined order
    lib_list = list(map(lambda lib_name: lib_dict[lib_name], lib_names))
    target_list = list(map(lambda lib_name: lib_dict[lib_name], target_names))
    test_list = list(map(lambda lib_name: lib_dict[lib_name], test_names))

    # get rid of temporary private fields prefixed with "_" and some other useless fields
    for lib in lib_list:
        for field_to_remove in filter(lambda k: k.startswith('_'), lib.keys()):
            lib.pop(field_to_remove, None)
    for target in target_list:
        for field_to_remove in filter(lambda k: k.startswith('_'),
                                      target.keys()):
            target.pop(field_to_remove, None)
        target.pop('public_headers',
                   None)  # public headers make no sense for targets
    for test in test_list:
        for field_to_remove in filter(lambda k: k.startswith('_'), test.keys()):
            test.pop(field_to_remove, None)
        test.pop('public_headers',
                 None)  # public headers make no sense for tests

    build_yaml_like = {
        'libs': lib_list,
        'filegroups': [],
        'targets': target_list,
        'tests': test_list,
    }
    return build_yaml_like


def _extract_cc_tests(bazel_rules):
    """Gets list of cc_test tests from bazel rules"""
    result = []
    for bazel_rule in bazel_rules.itervalues():
        if bazel_rule['class'] == 'cc_test':
            test_name = bazel_rule['name']
            if test_name.startswith('//'):
                prefixlen = len('//')
                result.append(test_name[prefixlen:])
    return list(sorted(result))


def _filter_cc_tests(tests):
    """Filters out tests that we don't want or we cannot build them reasonably"""

    # most qps tests are autogenerated, we are fine without them
    tests = list(
        filter(lambda test: not test.startswith('test/cpp/qps:'), tests))

    # we have trouble with census dependency outside of bazel
    tests = list(
        filter(lambda test: not test.startswith('test/cpp/ext/filters/census:'),
               tests))
    tests = list(
        filter(
            lambda test: not test.startswith(
                'test/cpp/microbenchmarks:bm_opencensus_plugin'), tests))

    # missing opencensus/stats/stats.h
    tests = list(
        filter(
            lambda test: not test.startswith(
                'test/cpp/end2end:server_load_reporting_end2end_test'), tests))
    tests = list(
        filter(
            lambda test: not test.startswith(
                'test/cpp/server/load_reporter:lb_load_reporter_test'), tests))

    # The test uses --running_under_bazel cmdline argument
    # To avoid the trouble needing to adjust it, we just skip the test
    tests = list(
        filter(
            lambda test: not test.startswith(
                'test/cpp/naming:resolver_component_tests_runner_invoker'),
            tests))

    # the test requires 'client_crash_test_server' to be built
    tests = list(
        filter(
            lambda test: not test.startswith('test/cpp/end2end:time_change_test'
                                            ), tests))

    # the test requires 'client_crash_test_server' to be built
    tests = list(
        filter(
            lambda test: not test.startswith(
                'test/cpp/end2end:client_crash_test'), tests))

    # the test requires 'server_crash_test_client' to be built
    tests = list(
        filter(
            lambda test: not test.startswith(
                'test/cpp/end2end:server_crash_test'), tests))

    # test never existed under build.yaml and it fails -> skip it
    tests = list(
        filter(
            lambda test: not test.startswith(
                'test/core/tsi:ssl_session_cache_test'), tests))

    return tests


def _generate_build_extra_metadata_for_tests(tests, bazel_rules):
    test_metadata = {}
    for test in tests:
        test_dict = {'build': 'test', '_TYPE': 'target'}

        bazel_rule = bazel_rules[_get_bazel_label(test)]

        bazel_tags = bazel_rule['tags']
        if 'manual' in bazel_tags:
            # don't run the tests marked as "manual"
            test_dict['run'] = False

        if 'no_uses_polling' in bazel_tags:
            test_dict['uses_polling'] = False

        if 'grpc_fuzzer' == bazel_rule['generator_function']:
            # currently we hand-list fuzzers instead of generating them automatically
            # because there's no way to obtain maxlen property from bazel BUILD file.
            print('skipping fuzzer ' + test)
            continue

        # if any tags that restrict platform compatibility are present,
        # generate the "platforms" field accordingly
        # TODO(jtattermusch): there is also a "no_linux" tag, but we cannot take
        # it into account as it is applied by grpc_cc_test when poller expansion
        # is made (for tests where uses_polling=True). So for now, we just
        # assume all tests are compatible with linux and ignore the "no_linux" tag
        # completely.
        known_platform_tags = set(['no_windows', 'no_mac'])
        if set(bazel_tags).intersection(known_platform_tags):
            platforms = []
            # assume all tests are compatible with linux and posix
            platforms.append('linux')
            platforms.append(
                'posix')  # there is no posix-specific tag in bazel BUILD
            if not 'no_mac' in bazel_tags:
                platforms.append('mac')
            if not 'no_windows' in bazel_tags:
                platforms.append('windows')
            test_dict['platforms'] = platforms

        if '//external:benchmark' in bazel_rule['transitive_deps']:
            test_dict['benchmark'] = True
            test_dict['defaults'] = 'benchmark'

        cmdline_args = bazel_rule['args']
        if cmdline_args:
            test_dict['args'] = list(cmdline_args)

        uses_gtest = '//external:gtest' in bazel_rule['transitive_deps']
        if uses_gtest:
            test_dict['gtest'] = True

        if test.startswith('test/cpp') or uses_gtest:
            test_dict['language'] = 'c++'

        elif test.startswith('test/core'):
            test_dict['language'] = 'c'
        else:
            raise Exception('wrong test' + test)

        # short test name without the path.
        # There can be name collisions, but we will resolve them later
        simple_test_name = os.path.basename(_extract_source_file_path(test))
        test_dict['_RENAME'] = simple_test_name

        test_metadata[test] = test_dict

    # detect duplicate test names
    tests_by_simple_name = {}
    for test_name, test_dict in test_metadata.iteritems():
        simple_test_name = test_dict['_RENAME']
        if not simple_test_name in tests_by_simple_name:
            tests_by_simple_name[simple_test_name] = []
        tests_by_simple_name[simple_test_name].append(test_name)

    # choose alternative names for tests with a name collision
    for collision_list in tests_by_simple_name.itervalues():
        if len(collision_list) > 1:
            for test_name in collision_list:
                long_name = test_name.replace('/', '_').replace(':', '_')
                print(
                    'short name of "%s" collides with another test, renaming to %s'
                    % (test_name, long_name))
                test_metadata[test_name]['_RENAME'] = long_name

    # TODO(jtattermusch): in bazel, add "_test" suffix to the test names
    # test does not have "_test" suffix: fling
    # test does not have "_test" suffix: fling_stream
    # test does not have "_test" suffix: client_ssl
    # test does not have "_test" suffix: handshake_server_with_readahead_handshaker
    # test does not have "_test" suffix: handshake_verify_peer_options
    # test does not have "_test" suffix: server_ssl

    return test_metadata


# extra metadata that will be used to construct build.yaml
# there are mostly extra properties that we weren't able to obtain from the bazel build
# _TYPE: whether this is library, target or test
# _RENAME: whether this target should be renamed to a different name (to match expectations of make and cmake builds)
# NOTE: secure is 'check' by default, so setting secure = False below does matter
_BUILD_EXTRA_METADATA = {
    'third_party/address_sorting:address_sorting': {
        'language': 'c',
        'build': 'all',
        'secure': False,
        '_RENAME': 'address_sorting'
    },
    'gpr': {
        'language': 'c',
        'build': 'all',
        'secure': False
    },
    'grpc': {
        'language': 'c',
        'build': 'all',
        'baselib': True,
        'secure': True,
        'dll': True,
        'generate_plugin_registry': True
    },
    'grpc++': {
        'language': 'c++',
        'build': 'all',
        'baselib': True,
        'dll': True
    },
    'grpc++_alts': {
        'language': 'c++',
        'build': 'all',
        'baselib': True
    },
    'grpc++_error_details': {
        'language': 'c++',
        'build': 'all'
    },
    'grpc++_reflection': {
        'language': 'c++',
        'build': 'all'
    },
    'grpc++_unsecure': {
        'language': 'c++',
        'build': 'all',
        'baselib': True,
        'secure': False,
        'dll': True
    },
    # TODO(jtattermusch): do we need to set grpc_csharp_ext's LDFLAGS for wrapping memcpy in the same way as in build.yaml?
    'grpc_csharp_ext': {
        'language': 'c',
        'build': 'all',
        'dll': 'only'
    },
    'grpc_unsecure': {
        'language': 'c',
        'build': 'all',
        'baselib': True,
        'secure': False,
        'dll': True,
        'generate_plugin_registry': True
    },
    'grpcpp_channelz': {
        'language': 'c++',
        'build': 'all'
    },
    'src/compiler:grpc_plugin_support': {
        'language': 'c++',
        'build': 'protoc',
        'secure': False,
        '_RENAME': 'grpc_plugin_support'
    },
    'src/compiler:grpc_cpp_plugin': {
        'language': 'c++',
        'build': 'protoc',
        'secure': False,
        '_TYPE': 'target',
        '_RENAME': 'grpc_cpp_plugin'
    },
    'src/compiler:grpc_csharp_plugin': {
        'language': 'c++',
        'build': 'protoc',
        'secure': False,
        '_TYPE': 'target',
        '_RENAME': 'grpc_csharp_plugin'
    },
    'src/compiler:grpc_node_plugin': {
        'language': 'c++',
        'build': 'protoc',
        'secure': False,
        '_TYPE': 'target',
        '_RENAME': 'grpc_node_plugin'
    },
    'src/compiler:grpc_objective_c_plugin': {
        'language': 'c++',
        'build': 'protoc',
        'secure': False,
        '_TYPE': 'target',
        '_RENAME': 'grpc_objective_c_plugin'
    },
    'src/compiler:grpc_php_plugin': {
        'language': 'c++',
        'build': 'protoc',
        'secure': False,
        '_TYPE': 'target',
        '_RENAME': 'grpc_php_plugin'
    },
    'src/compiler:grpc_python_plugin': {
        'language': 'c++',
        'build': 'protoc',
        'secure': False,
        '_TYPE': 'target',
        '_RENAME': 'grpc_python_plugin'
    },
    'src/compiler:grpc_ruby_plugin': {
        'language': 'c++',
        'build': 'protoc',
        'secure': False,
        '_TYPE': 'target',
        '_RENAME': 'grpc_ruby_plugin'
    },

    # TODO(jtattermusch): consider adding grpc++_core_stats

    # test support libraries
    'test/core/util:grpc_test_util': {
        'language': 'c',
        'build': 'private',
        '_RENAME': 'grpc_test_util'
    },
    'test/core/util:grpc_test_util_unsecure': {
        'language': 'c',
        'build': 'private',
        'secure': False,
        '_RENAME': 'grpc_test_util_unsecure'
    },
    # TODO(jtattermusch): consider adding grpc++_test_util_unsecure - it doesn't seem to be used by bazel build (don't forget to set secure: False)
    'test/cpp/util:test_config': {
        'language': 'c++',
        'build': 'private',
        '_RENAME': 'grpc++_test_config'
    },
    'test/cpp/util:test_util': {
        'language': 'c++',
        'build': 'private',
        '_RENAME': 'grpc++_test_util'
    },

    # end2end test support libraries
    'test/core/end2end:end2end_tests': {
        'language': 'c',
        'build': 'private',
        'secure': True,
        '_RENAME': 'end2end_tests'
    },
    'test/core/end2end:end2end_nosec_tests': {
        'language': 'c',
        'build': 'private',
        'secure': False,
        '_RENAME': 'end2end_nosec_tests'
    },

    # benchmark support libraries
    'test/cpp/microbenchmarks:helpers': {
        'language': 'c++',
        'build': 'test',
        'defaults': 'benchmark',
        '_RENAME': 'benchmark_helpers'
    },
    'test/cpp/interop:interop_client': {
        'language': 'c++',
        'build': 'test',
        'run': False,
        '_TYPE': 'target',
        '_RENAME': 'interop_client'
    },
    'test/cpp/interop:interop_server': {
        'language': 'c++',
        'build': 'test',
        'run': False,
        '_TYPE': 'target',
        '_RENAME': 'interop_server'
    },
    'test/cpp/interop:http2_client': {
        'language': 'c++',
        'build': 'test',
        'run': False,
        '_TYPE': 'target',
        '_RENAME': 'http2_client'
    },
    'test/cpp/qps:qps_json_driver': {
        'language': 'c++',
        'build': 'test',
        'run': False,
        '_TYPE': 'target',
        '_RENAME': 'qps_json_driver'
    },
    'test/cpp/qps:qps_worker': {
        'language': 'c++',
        'build': 'test',
        'run': False,
        '_TYPE': 'target',
        '_RENAME': 'qps_worker'
    },
    'test/cpp/util:grpc_cli': {
        'language': 'c++',
        'build': 'test',
        'run': False,
        '_TYPE': 'target',
        '_RENAME': 'grpc_cli'
    },

    # TODO(jtattermusch): create_jwt and verify_jwt breaks distribtests because it depends on grpc_test_utils and thus requires tests to be built
    # For now it's ok to disable them as these binaries aren't very useful anyway.
    #'test/core/security:create_jwt': { 'language': 'c', 'build': 'tool', '_TYPE': 'target', '_RENAME': 'grpc_create_jwt' },
    #'test/core/security:verify_jwt': { 'language': 'c', 'build': 'tool', '_TYPE': 'target', '_RENAME': 'grpc_verify_jwt' },

    # TODO(jtattermusch): add remaining tools such as grpc_print_google_default_creds_token (they are not used by bazel build)

    # Fuzzers
    'test/core/security:alts_credentials_fuzzer': {
        'language': 'c++',
        'build': 'fuzzer',
        'corpus_dirs': ['test/core/security/corpus/alts_credentials_corpus'],
        'maxlen': 2048,
        '_TYPE': 'target',
        '_RENAME': 'alts_credentials_fuzzer'
    },
    'test/core/end2end/fuzzers:client_fuzzer': {
        'language': 'c++',
        'build': 'fuzzer',
        'corpus_dirs': ['test/core/end2end/fuzzers/client_fuzzer_corpus'],
        'maxlen': 2048,
        'dict': 'test/core/end2end/fuzzers/hpack.dictionary',
        '_TYPE': 'target',
        '_RENAME': 'client_fuzzer'
    },
    'test/core/transport/chttp2:hpack_parser_fuzzer': {
        'language': 'c++',
        'build': 'fuzzer',
        'corpus_dirs': ['test/core/transport/chttp2/hpack_parser_corpus'],
        'maxlen': 512,
        'dict': 'test/core/end2end/fuzzers/hpack.dictionary',
        '_TYPE': 'target',
        '_RENAME': 'hpack_parser_fuzzer_test'
    },
    'test/core/http:request_fuzzer': {
        'language': 'c++',
        'build': 'fuzzer',
        'corpus_dirs': ['test/core/http/request_corpus'],
        'maxlen': 2048,
        '_TYPE': 'target',
        '_RENAME': 'http_request_fuzzer_test'
    },
    'test/core/http:response_fuzzer': {
        'language': 'c++',
        'build': 'fuzzer',
        'corpus_dirs': ['test/core/http/response_corpus'],
        'maxlen': 2048,
        '_TYPE': 'target',
        '_RENAME': 'http_response_fuzzer_test'
    },
    'test/core/json:json_fuzzer': {
        'language': 'c++',
        'build': 'fuzzer',
        'corpus_dirs': ['test/core/json/corpus'],
        'maxlen': 512,
        '_TYPE': 'target',
        '_RENAME': 'json_fuzzer_test'
    },
    'test/core/nanopb:fuzzer_response': {
        'language': 'c++',
        'build': 'fuzzer',
        'corpus_dirs': ['test/core/nanopb/corpus_response'],
        'maxlen': 128,
        '_TYPE': 'target',
        '_RENAME': 'nanopb_fuzzer_response_test'
    },
    'test/core/nanopb:fuzzer_serverlist': {
        'language': 'c++',
        'build': 'fuzzer',
        'corpus_dirs': ['test/core/nanopb/corpus_serverlist'],
        'maxlen': 128,
        '_TYPE': 'target',
        '_RENAME': 'nanopb_fuzzer_serverlist_test'
    },
    'test/core/slice:percent_decode_fuzzer': {
        'language': 'c++',
        'build': 'fuzzer',
        'corpus_dirs': ['test/core/slice/percent_decode_corpus'],
        'maxlen': 32,
        '_TYPE': 'target',
        '_RENAME': 'percent_decode_fuzzer'
    },
    'test/core/slice:percent_encode_fuzzer': {
        'language': 'c++',
        'build': 'fuzzer',
        'corpus_dirs': ['test/core/slice/percent_encode_corpus'],
        'maxlen': 32,
        '_TYPE': 'target',
        '_RENAME': 'percent_encode_fuzzer'
    },
    'test/core/end2end/fuzzers:server_fuzzer': {
        'language': 'c++',
        'build': 'fuzzer',
        'corpus_dirs': ['test/core/end2end/fuzzers/server_fuzzer_corpus'],
        'maxlen': 2048,
        'dict': 'test/core/end2end/fuzzers/hpack.dictionary',
        '_TYPE': 'target',
        '_RENAME': 'server_fuzzer'
    },
    'test/core/security:ssl_server_fuzzer': {
        'language': 'c++',
        'build': 'fuzzer',
        'corpus_dirs': ['test/core/security/corpus/ssl_server_corpus'],
        'maxlen': 2048,
        '_TYPE': 'target',
        '_RENAME': 'ssl_server_fuzzer'
    },
    'test/core/client_channel:uri_fuzzer_test': {
        'language': 'c++',
        'build': 'fuzzer',
        'corpus_dirs': ['test/core/client_channel/uri_corpus'],
        'maxlen': 128,
        '_TYPE': 'target',
        '_RENAME': 'uri_fuzzer_test'
    },

    # TODO(jtattermusch): these fuzzers had no build.yaml equivalent
    # test/core/compression:message_compress_fuzzer
    # test/core/compression:message_decompress_fuzzer
    # test/core/compression:stream_compression_fuzzer
    # test/core/compression:stream_decompression_fuzzer
    # test/core/slice:b64_decode_fuzzer
    # test/core/slice:b64_encode_fuzzer
}

# We need a complete picture of all the targets and dependencies we're interested in
# so we run multiple bazel queries and merge the results.
_BAZEL_DEPS_QUERIES = [
    'deps("//test/...")',
    'deps("//:all")',
    'deps("//src/compiler/...")',
    'deps("//src/proto/...")',
]

bazel_rules = {}
for query in _BAZEL_DEPS_QUERIES:
    bazel_rules.update(
        _extract_rules_from_bazel_xml(_bazel_query_xml_tree(query)))

_populate_transitive_deps(bazel_rules)

tests = _filter_cc_tests(_extract_cc_tests(bazel_rules))
test_metadata = _generate_build_extra_metadata_for_tests(tests, bazel_rules)

all_metadata = {}
all_metadata.update(_BUILD_EXTRA_METADATA)
all_metadata.update(test_metadata)

all_targets_dict = _generate_build_metadata(all_metadata, bazel_rules)
build_yaml_like = _convert_to_build_yaml_like(all_targets_dict)

# if a test uses source files from src/ directly, it's a little bit suspicious
for tgt in build_yaml_like['targets']:
    if tgt['build'] == 'test':
        for src in tgt['src']:
            if src.startswith('src/') and not src.endswith('.proto'):
                print('source file from under "src/" tree used in test ' +
                      tgt['name'] + ': ' + src)

build_yaml_string = build_cleaner.cleaned_build_yaml_dict_as_string(
    build_yaml_like)
with open('build_autogenerated.yaml', 'w') as file:
    file.write(build_yaml_string)
