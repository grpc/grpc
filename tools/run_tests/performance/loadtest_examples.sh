#!/bin/bash
# Copyright 2021 The gRPC Authors
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

# This script generates a set of load test examples from templates.

LOADTEST_CONFIG=tools/run_tests/performance/loadtest_config.py

if (( $# < 1 )); then
    echo "Usage: ${0} <output directory>" >&2
    exit 1
fi

if [[ ! -x "${LOADTEST_CONFIG}" ]]; then
    echo "${LOADTEST_CONFIG} not found." >&2
    exit 1
fi

outputbasedir="${1}"

mkdir -p "${outputbasedir}/templates"

example_file() {
    local scenario="${1}"
    local suffix="${2}"
    if [[ "${scenario#cpp_}" != "${scenario}" ]]; then
        echo "cxx${suffix}"
        return
    fi
    if [[ "${scenario#python_asyncio_}" != "${scenario}" ]]; then
        echo "python_asyncio${suffix}"
        return
    fi
    echo "${scenario%%_*}${suffix}"
}

example_language() {
    local filename="${1}"
    if [[ "${filename#cxx_}" != "${filename}" ]]; then
        echo "c++"
        return
    fi
    if [[ "${filename#python_asyncio_}" != "${filename}" ]]; then
        echo "python_asyncio"
        return
    fi
    echo "${filename%%_*}"
}

scenarios=(
    "cpp_generic_async_streaming_ping_pong_secure"
    "csharp_protobuf_async_unary_ping_pong"
    "go_generic_sync_streaming_ping_pong_secure"
    "java_generic_async_streaming_ping_pong_secure"
    "node_to_node_generic_async_streaming_ping_pong_secure"
    "php7_protobuf_php_extension_to_cpp_protobuf_sync_unary_ping_pong"
    "python_generic_sync_streaming_ping_pong"
    "python_asyncio_generic_async_streaming_ping_pong"
    "ruby_protobuf_sync_streaming_ping_pong"
)

# Basic examples are intended to be runnable _as is_, so substitution keys
# are stripped. Fields can be inserted manually following the pattern of the
# prebuilt examples.
basic_example() {
    local -r scenario="${1}"
    local -r outputdir="${2}"
    local -r outputfile="$(example_file "${scenario}" _example_loadtest.yaml)"
    local -r language="$(example_language "${outputfile}")"
    ${LOADTEST_CONFIG} \
        -l "${language}" \
        -t ./tools/run_tests/performance/templates/loadtest_template_basic_all_languages.yaml \
        -s client_pool= -s server_pool= -s big_query_table= \
        -s timeout_seconds=900 --prefix=examples -u basic -r "^${scenario}$" \
        --allow_client_language=c++ --allow_server_language=c++ \
        --allow_server_language=node \
        -o "${outputdir}/${outputfile}"
    echo "Created example: ${outputfile}"
}

# Prebuilt examples contain substitution keys, so must be processed before
# running.
prebuilt_example() {
    local -r scenario="${1}"
    local -r outputdir="${2}"
    local -r outputfile="$(example_file "${scenario}" _example_loadtest_with_prebuilt_workers.yaml)"
    local -r language="$(example_language "${outputfile}")"
    ${LOADTEST_CONFIG} \
        -l "${language}" \
        -t ./tools/run_tests/performance/templates/loadtest_template_prebuilt_all_languages.yaml \
        -s driver_pool="\${driver_pool}" -s driver_image="\${driver_image}" \
        -s client_pool="\${workers_pool}" -s server_pool="\${workers_pool}" \
        -s big_query_table="\${big_query_table}" -s timeout_seconds=900 \
        -s prebuilt_image_prefix="\${prebuilt_image_prefix}" \
        -s prebuilt_image_tag="\${prebuilt_image_tag}" --prefix=examples -u prebuilt \
        -a pool="\${workers_pool}" -r "^${scenario}$" \
        --allow_client_language=c++ --allow_server_language=c++ \
        --allow_server_language=node \
        -o "${outputdir}/${outputfile}"
    echo "Created example: ${outputfile}"
}

for scenario in "${scenarios[@]}"; do
    basic_example "${scenario}" "${outputbasedir}"
done

for scenario in "${scenarios[@]}"; do
    prebuilt_example "${scenario}" "${outputbasedir}/templates"
done
