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

import argparse
import collections
from doctest import SKIP
import multiprocessing
import os
import re
import sys

import run_buildozer

# find our home
ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(ROOT)

vendors = collections.defaultdict(list)
scores = collections.defaultdict(int)
avoidness = collections.defaultdict(int)
consumes = {}
no_update = set()
buildozer_commands = []
original_deps = {}
original_external_deps = {}
skip_headers = collections.defaultdict(set)

# TODO(ctiller): ideally we wouldn't hardcode a bunch of paths here.
# We can likely parse out BUILD files from dependencies to generate this index.
EXTERNAL_DEPS = {
    'absl/algorithm/container.h':
        'absl/algorithm:container',
    'absl/base/attributes.h':
        'absl/base:core_headers',
    'absl/base/call_once.h':
        'absl/base',
    # TODO(ctiller) remove this
    'absl/base/internal/endian.h':
        'absl/base',
    'absl/base/thread_annotations.h':
        'absl/base:core_headers',
    'absl/container/flat_hash_map.h':
        'absl/container:flat_hash_map',
    'absl/container/flat_hash_set.h':
        'absl/container:flat_hash_set',
    'absl/container/inlined_vector.h':
        'absl/container:inlined_vector',
    'absl/cleanup/cleanup.h':
        'absl/cleanup',
    'absl/debugging/failure_signal_handler.h':
        'absl/debugging:failure_signal_handler',
    'absl/debugging/stacktrace.h':
        'absl/debugging:stacktrace',
    'absl/debugging/symbolize.h':
        'absl/debugging:symbolize',
    'absl/flags/flag.h':
        'absl/flags:flag',
    'absl/flags/marshalling.h':
        'absl/flags:marshalling',
    'absl/flags/parse.h':
        'absl/flags:parse',
    'absl/functional/any_invocable.h':
        'absl/functional:any_invocable',
    'absl/functional/bind_front.h':
        'absl/functional:bind_front',
    'absl/functional/function_ref.h':
        'absl/functional:function_ref',
    'absl/hash/hash.h':
        'absl/hash',
    'absl/memory/memory.h':
        'absl/memory',
    'absl/meta/type_traits.h':
        'absl/meta:type_traits',
    'absl/numeric/int128.h':
        'absl/numeric:int128',
    'absl/random/random.h':
        'absl/random',
    'absl/random/distributions.h':
        'absl/random:distributions',
    'absl/random/uniform_int_distribution.h':
        'absl/random:distributions',
    'absl/status/status.h':
        'absl/status',
    'absl/status/statusor.h':
        'absl/status:statusor',
    'absl/strings/ascii.h':
        'absl/strings',
    'absl/strings/cord.h':
        'absl/strings:cord',
    'absl/strings/escaping.h':
        'absl/strings',
    'absl/strings/match.h':
        'absl/strings',
    'absl/strings/numbers.h':
        'absl/strings',
    'absl/strings/str_cat.h':
        'absl/strings',
    'absl/strings/str_format.h':
        'absl/strings:str_format',
    'absl/strings/str_join.h':
        'absl/strings',
    'absl/strings/str_replace.h':
        'absl/strings',
    'absl/strings/str_split.h':
        'absl/strings',
    'absl/strings/string_view.h':
        'absl/strings',
    'absl/strings/strip.h':
        'absl/strings',
    'absl/strings/substitute.h':
        'absl/strings',
    'absl/synchronization/mutex.h':
        'absl/synchronization',
    'absl/synchronization/notification.h':
        'absl/synchronization',
    'absl/time/clock.h':
        'absl/time',
    'absl/time/time.h':
        'absl/time',
    'absl/types/optional.h':
        'absl/types:optional',
    'absl/types/span.h':
        'absl/types:span',
    'absl/types/variant.h':
        'absl/types:variant',
    'absl/utility/utility.h':
        'absl/utility',
    'address_sorting/address_sorting.h':
        'address_sorting',
    'ares.h':
        'cares',
    'fuzztest/fuzztest.h': ['fuzztest', 'fuzztest_main'],
    'google/api/monitored_resource.pb.h':
        'google/api:monitored_resource_cc_proto',
    'google/devtools/cloudtrace/v2/tracing.grpc.pb.h':
        'googleapis_trace_grpc_service',
    'google/logging/v2/logging.grpc.pb.h':
        'googleapis_logging_grpc_service',
    'google/logging/v2/logging.pb.h':
        'googleapis_logging_cc_proto',
    'google/logging/v2/log_entry.pb.h':
        'googleapis_logging_cc_proto',
    'google/monitoring/v3/metric_service.grpc.pb.h':
        'googleapis_monitoring_grpc_service',
    'gmock/gmock.h':
        'gtest',
    'gtest/gtest.h':
        'gtest',
    'opencensus/exporters/stats/stackdriver/stackdriver_exporter.h':
        'opencensus-stats-stackdriver_exporter',
    'opencensus/exporters/trace/stackdriver/stackdriver_exporter.h':
        'opencensus-trace-stackdriver_exporter',
    'opencensus/trace/context_util.h':
        'opencensus-trace-context_util',
    'opencensus/trace/propagation/grpc_trace_bin.h':
        'opencensus-trace-propagation',
    'opencensus/tags/context_util.h':
        'opencensus-tags-context_util',
    'opencensus/trace/span_context.h':
        'opencensus-trace-span_context',
    'openssl/base.h':
        'libssl',
    'openssl/bio.h':
        'libssl',
    'openssl/bn.h':
        'libcrypto',
    'openssl/buffer.h':
        'libcrypto',
    'openssl/crypto.h':
        'libcrypto',
    'openssl/digest.h':
        'libssl',
    'openssl/engine.h':
        'libcrypto',
    'openssl/err.h':
        'libcrypto',
    'openssl/evp.h':
        'libcrypto',
    'openssl/hmac.h':
        'libcrypto',
    'openssl/pem.h':
        'libcrypto',
    'openssl/rsa.h':
        'libcrypto',
    'openssl/sha.h':
        'libcrypto',
    'openssl/ssl.h':
        'libssl',
    'openssl/tls1.h':
        'libssl',
    'openssl/x509.h':
        'libcrypto',
    'openssl/x509v3.h':
        'libcrypto',
    're2/re2.h':
        're2',
    'upb/arena.h':
        'upb_lib',
    'upb/base/string_view.h':
        'upb_lib',
    'upb/collections/map.h':
        'upb_collections_lib',
    'upb/def.h':
        'upb_lib',
    'upb/json_encode.h':
        'upb_json_lib',
    'upb/mem/arena.h':
        'upb_lib',
    'upb/text_encode.h':
        'upb_textformat_lib',
    'upb/def.hpp':
        'upb_reflection',
    'upb/upb.h':
        'upb_lib',
    'upb/upb.hpp':
        'upb_lib',
    'xxhash.h':
        'xxhash',
    'zlib.h':
        'madler_zlib',
}

INTERNAL_DEPS = {
    "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h":
        "//test/core/event_engine/fuzzing_event_engine",
    "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h":
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
    'google/api/expr/v1alpha1/syntax.upb.h':
        'google_type_expr_upb',
    'google/rpc/status.upb.h':
        'google_rpc_status_upb',
    'google/protobuf/any.upb.h':
        'protobuf_any_upb',
    'google/protobuf/duration.upb.h':
        'protobuf_duration_upb',
    'google/protobuf/struct.upb.h':
        'protobuf_struct_upb',
    'google/protobuf/timestamp.upb.h':
        'protobuf_timestamp_upb',
    'google/protobuf/wrappers.upb.h':
        'protobuf_wrappers_upb',
    'grpc/status.h':
        'grpc_public_hdrs',
    'src/proto/grpc/channelz/channelz.grpc.pb.h':
        '//src/proto/grpc/channelz:channelz_proto',
    'src/proto/grpc/core/stats.pb.h':
        '//src/proto/grpc/core:stats_proto',
    'src/proto/grpc/health/v1/health.upb.h':
        'grpc_health_upb',
    'src/proto/grpc/lb/v1/load_reporter.grpc.pb.h':
        '//src/proto/grpc/lb/v1:load_reporter_proto',
    'src/proto/grpc/lb/v1/load_balancer.upb.h':
        'grpc_lb_upb',
    'src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h':
        '//src/proto/grpc/reflection/v1alpha:reflection_proto',
    'src/proto/grpc/gcp/transport_security_common.upb.h':
        'alts_upb',
    'src/proto/grpc/gcp/handshaker.upb.h':
        'alts_upb',
    'src/proto/grpc/gcp/altscontext.upb.h':
        'alts_upb',
    'src/proto/grpc/lookup/v1/rls.upb.h':
        'rls_upb',
    'src/proto/grpc/lookup/v1/rls_config.upb.h':
        'rls_config_upb',
    'src/proto/grpc/lookup/v1/rls_config.upbdefs.h':
        'rls_config_upbdefs',
    'src/proto/grpc/testing/xds/v3/csds.grpc.pb.h':
        '//src/proto/grpc/testing/xds/v3:csds_proto',
    'xds/data/orca/v3/orca_load_report.upb.h':
        'xds_orca_upb',
    'xds/service/orca/v3/orca.upb.h':
        'xds_orca_service_upb',
    'xds/type/v3/typed_struct.upb.h':
        'xds_type_upb',
}


class FakeSelects:

    def config_setting_group(self, **kwargs):
        pass


num_cc_libraries = 0
num_opted_out_cc_libraries = 0
parsing_path = None


# Convert the source or header target to a relative path.
def _get_filename(name, parsing_path):
    filename = '%s%s' % (
        (parsing_path + '/' if
         (parsing_path and not name.startswith('//')) else ''), name)
    filename = filename.replace('//:', '')
    filename = filename.replace('//src/core:', 'src/core/')
    filename = filename.replace('//src/cpp/ext/filters/census:',
                                'src/cpp/ext/filters/census/')
    return filename


def grpc_cc_library(name,
                    hdrs=[],
                    public_hdrs=[],
                    srcs=[],
                    select_deps=None,
                    tags=[],
                    deps=[],
                    external_deps=[],
                    proto=None,
                    **kwargs):
    global args
    global num_cc_libraries
    global num_opted_out_cc_libraries
    global parsing_path
    assert (parsing_path is not None)
    name = '//%s:%s' % (parsing_path, name)
    num_cc_libraries += 1
    if select_deps or 'nofixdeps' in tags:
        if args.whats_left and not select_deps and 'nofixdeps' not in tags:
            num_opted_out_cc_libraries += 1
            print("Not opted in: {}".format(name))
        no_update.add(name)
    scores[name] = len(public_hdrs + hdrs)
    # avoid_dep is the internal way of saying prefer something else
    # we add grpc_avoid_dep to allow internal grpc-only stuff to avoid each
    # other, whilst not biasing dependent projects
    if 'avoid_dep' in tags or 'grpc_avoid_dep' in tags:
        avoidness[name] += 10
    if proto:
        proto_hdr = '%s%s' % ((parsing_path + '/' if parsing_path else ''),
                              proto.replace('.proto', '.pb.h'))
        skip_headers[name].add(proto_hdr)

    for hdr in hdrs + public_hdrs:
        vendors[_get_filename(hdr, parsing_path)].append(name)
    inc = set()
    original_deps[name] = frozenset(deps)
    original_external_deps[name] = frozenset(external_deps)
    for src in hdrs + public_hdrs + srcs:
        for line in open(_get_filename(src, parsing_path)):
            m = re.search(r'^#include <(.*)>', line)
            if m:
                inc.add(m.group(1))
            m = re.search(r'^#include "(.*)"', line)
            if m:
                inc.add(m.group(1))
    consumes[name] = list(inc)


def grpc_proto_library(name, srcs, **kwargs):
    global parsing_path
    assert (parsing_path is not None)
    name = '//%s:%s' % (parsing_path, name)
    for src in srcs:
        proto_hdr = src.replace('.proto', '.pb.h')
        vendors[_get_filename(proto_hdr, parsing_path)].append(name)


def buildozer(cmd, target):
    buildozer_commands.append('%s|%s' % (cmd, target))


def buildozer_set_list(name, values, target, via=""):
    if not values:
        buildozer('remove %s' % name, target)
        return
    adjust = via if via else name
    buildozer('set %s %s' % (adjust, ' '.join('"%s"' % s for s in values)),
              target)
    if via:
        buildozer('remove %s' % name, target)
        buildozer('rename %s %s' % (via, name), target)


def score_edit_distance(proposed, existing):
    """Score a proposed change primarily by edit distance"""
    sum = 0
    for p in proposed:
        if p not in existing:
            sum += 1
    for e in existing:
        if e not in proposed:
            sum += 1
    return sum


def total_score(proposal):
    return sum(scores[dep] for dep in proposal)


def total_avoidness(proposal):
    return sum(avoidness[dep] for dep in proposal)


def score_list_size(proposed, existing):
    """Score a proposed change primarily by number of dependencies"""
    return len(proposed)


def score_best(proposed, existing):
    """Score a proposed change primarily by dependency score"""
    return 0


SCORERS = {
    'edit_distance': score_edit_distance,
    'list_size': score_list_size,
    'best': score_best,
}

parser = argparse.ArgumentParser(description='Fix build dependencies')
parser.add_argument('targets',
                    nargs='*',
                    default=[],
                    help='targets to fix (empty => all)')
parser.add_argument('--score',
                    type=str,
                    default='edit_distance',
                    help='scoring function to use: one of ' +
                    ', '.join(SCORERS.keys()))
parser.add_argument('--whats_left',
                    action='store_true',
                    default=False,
                    help='show what is left to opt in')
parser.add_argument('--explain',
                    action='store_true',
                    default=False,
                    help='try to explain some decisions')
parser.add_argument(
    '--why',
    type=str,
    default=None,
    help='with --explain, target why a given dependency is needed')
args = parser.parse_args()

for dirname in [
        "",
        "src/core",
        "src/cpp/ext/gcp",
        "test/core/backoff",
        "test/core/uri",
        "test/core/util",
        "test/core/end2end",
        "test/core/event_engine",
        "test/core/filters",
        "test/core/promise",
        "test/core/resource_quota",
        "test/core/transport/chaotic_good",
        "fuzztest",
        "fuzztest/core/channel",
]:
    parsing_path = dirname
    exec(
        open('%sBUILD' % (dirname + '/' if dirname else ''), 'r').read(), {
            'load': lambda filename, *args: None,
            'licenses': lambda licenses: None,
            'package': lambda **kwargs: None,
            'exports_files': lambda files, visibility=None: None,
            'bool_flag': lambda **kwargs: None,
            'config_setting': lambda **kwargs: None,
            'selects': FakeSelects(),
            'python_config_settings': lambda **kwargs: None,
            'grpc_cc_binary': grpc_cc_library,
            'grpc_cc_library': grpc_cc_library,
            'grpc_cc_test': grpc_cc_library,
            'grpc_fuzzer': grpc_cc_library,
            'grpc_fuzz_test': grpc_cc_library,
            'grpc_proto_fuzzer': grpc_cc_library,
            'grpc_proto_library': grpc_proto_library,
            'select': lambda d: d["//conditions:default"],
            'glob': lambda files: None,
            'grpc_end2end_tests': lambda: None,
            'grpc_upb_proto_library': lambda name, **kwargs: None,
            'grpc_upb_proto_reflection_library': lambda name, **kwargs: None,
            'grpc_generate_one_off_targets': lambda: None,
            'grpc_package': lambda **kwargs: None,
            'filegroup': lambda name, **kwargs: None,
            'sh_library': lambda name, **kwargs: None,
        }, {})
    parsing_path = None

if args.whats_left:
    print("{}/{} libraries are opted in".format(
        num_cc_libraries - num_opted_out_cc_libraries, num_cc_libraries))


def make_relative_path(dep, lib):
    if lib is None:
        return dep
    lib_path = lib[:lib.rfind(':') + 1]
    if dep.startswith(lib_path):
        return dep[len(lib_path):]
    return dep


if args.whats_left:
    print("{}/{} libraries are opted in".format(
        num_cc_libraries - num_opted_out_cc_libraries, num_cc_libraries))


# Keeps track of all possible sets of dependencies that could satify the
# problem. (models the list monad in Haskell!)
class Choices:

    def __init__(self, library, substitutions):
        self.library = library
        self.to_add = []
        self.to_remove = []
        self.substitutions = substitutions

    def add_one_of(self, choices, trigger):
        if not choices:
            return
        choices = sum([self.apply_substitutions(choice) for choice in choices],
                      [])
        if args.explain and (args.why is None or args.why in choices):
            print("{}: Adding one of {} for {}".format(self.library, choices,
                                                       trigger))
        self.to_add.append(
            tuple(
                make_relative_path(choice, self.library) for choice in choices))

    def add(self, choice, trigger):
        self.add_one_of([choice], trigger)

    def remove(self, remove):
        for remove in self.apply_substitutions(remove):
            self.to_remove.append(make_relative_path(remove, self.library))

    def apply_substitutions(self, dep):
        if dep in self.substitutions:
            return self.substitutions[dep]
        return [dep]

    def best(self, scorer):
        choices = set()
        choices.add(frozenset())

        for add in sorted(set(self.to_add), key=lambda x: (len(x), x)):
            new_choices = set()
            for append_choice in add:
                for choice in choices:
                    new_choices.add(choice.union([append_choice]))
            choices = new_choices
        for remove in sorted(set(self.to_remove)):
            new_choices = set()
            for choice in choices:
                new_choices.add(choice.difference([remove]))
            choices = new_choices

        best = None

        def final_scorer(x):
            return (total_avoidness(x), scorer(x), total_score(x))

        for choice in choices:
            if best is None or final_scorer(choice) < final_scorer(best):
                best = choice
        return best


def make_library(library):
    error = False
    hdrs = sorted(consumes[library])
    # we need a little trickery here since grpc_base has channel.cc, which calls grpc_init
    # which is in grpc, which is illegal but hard to change
    # once EventEngine lands we can clean this up
    deps = Choices(library, {'//:grpc_base': ['//:grpc', '//:grpc_unsecure']}
                   if library.startswith('//test/') else {})
    external_deps = Choices(None, {})
    for hdr in hdrs:
        if hdr in skip_headers[library]:
            continue

        if hdr == 'systemd/sd-daemon.h':
            continue

        if hdr == 'src/core/lib/profiling/stap_probes.h':
            continue

        if hdr.startswith('src/libfuzzer/'):
            continue

        if hdr == 'grpc/grpc.h' and library.startswith('//test:'):
            # not the root build including grpc.h ==> //:grpc
            deps.add_one_of(['//:grpc', '//:grpc_unsecure'], hdr)
            continue

        if hdr in INTERNAL_DEPS:
            dep = INTERNAL_DEPS[hdr]
            if isinstance(dep, list):
                for d in dep:
                    deps.add(d, hdr)
            else:
                if not ('//' in dep):
                    dep = '//:' + dep
                deps.add(dep, hdr)
            continue

        if hdr in vendors:
            deps.add_one_of(vendors[hdr], hdr)
            continue

        if 'include/' + hdr in vendors:
            deps.add_one_of(vendors['include/' + hdr], hdr)
            continue

        if '.' not in hdr:
            # assume a c++ system include
            continue

        if hdr in EXTERNAL_DEPS:
            if isinstance(EXTERNAL_DEPS[hdr], list):
                for dep in EXTERNAL_DEPS[hdr]:
                    external_deps.add(dep, hdr)
            else:
                external_deps.add(EXTERNAL_DEPS[hdr], hdr)
            continue

        if hdr.startswith('opencensus/'):
            trail = hdr[len('opencensus/'):]
            trail = trail[:trail.find('/')]
            external_deps.add('opencensus-' + trail, hdr)
            continue

        if hdr.startswith('envoy/'):
            path, file = os.path.split(hdr)
            file = file.split('.')
            path = path.split('/')
            dep = '_'.join(path[:-1] + [file[1]])
            deps.add(dep, hdr)
            continue

        if hdr.startswith('google/protobuf/') and not hdr.endswith('.upb.h'):
            external_deps.add('protobuf_headers', hdr)
            continue

        if '/' not in hdr:
            # assume a system include
            continue

        is_sys_include = False
        for sys_path in [
                'sys',
                'arpa',
                'gperftools',
                'netinet',
                'linux',
                'android',
                'mach',
                'net',
                'CoreFoundation',
        ]:
            if hdr.startswith(sys_path + '/'):
                is_sys_include = True
                break
        if is_sys_include:
            # assume a system include
            continue

        print("# ERROR: can't categorize header: %s used by %s" %
              (hdr, library))
        error = True

    deps.remove(library)

    deps = sorted(
        deps.best(lambda x: SCORERS[args.score](x, original_deps[library])))
    external_deps = sorted(
        external_deps.best(lambda x: SCORERS[args.score]
                           (x, original_external_deps[library])))

    return (library, error, deps, external_deps)


def main() -> None:
    update_libraries = []
    for library in sorted(consumes.keys()):
        if library in no_update:
            continue
        if args.targets and library not in args.targets:
            continue
        update_libraries.append(library)
    with multiprocessing.Pool(processes=multiprocessing.cpu_count()) as p:
        updated_libraries = p.map(make_library, update_libraries, 1)

    error = False
    for library, lib_error, deps, external_deps in updated_libraries:
        if lib_error:
            error = True
            continue
        buildozer_set_list('external_deps', external_deps, library, via='deps')
        buildozer_set_list('deps', deps, library)

    run_buildozer.run_buildozer(buildozer_commands)

    if error:
        sys.exit(1)


if __name__ == "__main__":
    main()
