#!/usr/bin/env python3
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

# This script generates a load test configuration template from a collection of
# load test configurations.
#
# Configuration templates contain client and server configurations for multiple
# languages, and may contain template substitution keys. These templates are
# used to generate load test configurations by selecting clients and servers for
# the required languages. The source files for template generation may be load
# test configurations or load test configuration templates. Load test
# configuration generation is performed by loadtest_config.py. See documentation
# below:
# https://github.com/grpc/grpc/blob/master/tools/run_tests/performance/README.md

import argparse
import os
import sys
from typing import Any, Dict, Iterable, List, Mapping, Type

import yaml

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import loadtest_config

TEMPLATE_FILE_HEADER_COMMENT = """
# Template generated from load test configurations by loadtest_template.py.
#
# Configuration templates contain client and server configurations for multiple
# languages, and may contain template substitution keys. These templates are
# used to generate load test configurations by selecting clients and servers for
# the required languages. The source files for template generation may be load
# test configurations or load test configuration templates. Load test
# configuration generation is performed by loadtest_config.py. See documentation
# below:
# https://github.com/grpc/grpc/blob/master/tools/run_tests/performance/README.md
"""


def insert_worker(
    worker: Dict[str, Any], workers: List[Dict[str, Any]]
) -> None:
    """Inserts client or server into a list, without inserting duplicates."""

    def dump(w):
        return yaml.dump(w, Dumper=yaml.SafeDumper, default_flow_style=False)

    worker_str = dump(worker)
    if any((worker_str == dump(w) for w in workers)):
        return
    workers.append(worker)


def uniquify_workers(workermap: Dict[str, List[Dict[str, Any]]]) -> None:
    """Name workers if there is more than one for the same map key."""
    for workers in list(workermap.values()):
        if len(workers) <= 1:
            continue
        for i, worker in enumerate(workers):
            worker["name"] = str(i)


def loadtest_template(
        input_file_names: Iterable[str],
        metadata: Mapping[str, Any],
        inject_client_pool: bool,
        inject_driver_image: bool,
        inject_driver_pool: bool,
        inject_server_pool: bool,
        inject_big_query_table: bool,
        inject_timeout_seconds: bool,
        inject_ttl_seconds: bool) -> Dict[str, Any]:  # fmt: skip
    """Generates the load test template."""
    spec = dict()  # type: Dict[str, Any]
    clientmap = dict()  # Dict[str, List[Dict[str, Any]]]
    servermap = dict()  # Dict[Str, List[Dict[str, Any]]]
    template = {
        "apiVersion": "e2etest.grpc.io/v1",
        "kind": "LoadTest",
        "metadata": metadata,
    }
    for input_file_name in input_file_names:
        with open(input_file_name) as f:
            input_config = yaml.safe_load(f.read())

            if input_config.get("apiVersion") != template["apiVersion"]:
                raise ValueError(
                    "Unexpected api version in file {}: {}".format(
                        input_file_name, input_config.get("apiVersion")
                    )
                )
            if input_config.get("kind") != template["kind"]:
                raise ValueError(
                    "Unexpected kind in file {}: {}".format(
                        input_file_name, input_config.get("kind")
                    )
                )

            for client in input_config["spec"]["clients"]:
                del client["name"]
                if inject_client_pool:
                    client["pool"] = "${client_pool}"
                if client["language"] not in clientmap:
                    clientmap[client["language"]] = []
                insert_worker(client, clientmap[client["language"]])

            for server in input_config["spec"]["servers"]:
                del server["name"]
                if inject_server_pool:
                    server["pool"] = "${server_pool}"
                if server["language"] not in servermap:
                    servermap[server["language"]] = []
                insert_worker(server, servermap[server["language"]])

            input_spec = input_config["spec"]
            del input_spec["clients"]
            del input_spec["servers"]
            del input_spec["scenariosJSON"]
            spec.update(input_config["spec"])

    uniquify_workers(clientmap)
    uniquify_workers(servermap)

    spec.update(
        {
            "clients": sum(
                (clientmap[language] for language in sorted(clientmap)),
                start=[],
            ),
            "servers": sum(
                (servermap[language] for language in sorted(servermap)),
                start=[],
            ),
        }
    )

    if "driver" not in spec:
        spec["driver"] = {"language": "cxx"}

    driver = spec["driver"]
    if "name" in driver:
        del driver["name"]
    if inject_driver_image:
        if "run" not in driver:
            driver["run"] = [{"name": "main"}]
        driver["run"][0]["image"] = "${driver_image}"
    if inject_driver_pool:
        driver["pool"] = "${driver_pool}"

    if "run" not in driver:
        if inject_driver_pool:
            raise ValueError("Cannot inject driver.pool: missing driver.run.")
        del spec["driver"]

    if inject_big_query_table:
        if "results" not in spec:
            spec["results"] = dict()
        spec["results"]["bigQueryTable"] = "${big_query_table}"
    if inject_timeout_seconds:
        spec["timeoutSeconds"] = "${timeout_seconds}"
    if inject_ttl_seconds:
        spec["ttlSeconds"] = "${ttl_seconds}"

    template["spec"] = spec

    return template


def template_dumper(header_comment: str) -> Type[yaml.SafeDumper]:
    """Returns a custom dumper to dump templates in the expected format."""

    class TemplateDumper(yaml.SafeDumper):
        def expect_stream_start(self):
            super().expect_stream_start()
            if isinstance(self.event, yaml.StreamStartEvent):
                self.write_indent()
                self.write_indicator(header_comment, need_whitespace=False)

    def str_presenter(dumper, data):
        if "\n" in data:
            return dumper.represent_scalar(
                "tag:yaml.org,2002:str", data, style="|"
            )
        return dumper.represent_scalar("tag:yaml.org,2002:str", data)

    TemplateDumper.add_representer(str, str_presenter)

    return TemplateDumper


def main() -> None:
    argp = argparse.ArgumentParser(
        description="Creates a load test config generator template.",
        fromfile_prefix_chars="@",
    )
    argp.add_argument(
        "-i",
        "--inputs",
        action="extend",
        nargs="+",
        type=str,
        help="Input files.",
    )
    argp.add_argument(
        "-o",
        "--output",
        type=str,
        help="Output file. Outputs to stdout if not set.",
    )
    argp.add_argument(
        "--inject_client_pool",
        action="store_true",
        help="Set spec.client(s).pool values to '${client_pool}'.",
    )
    argp.add_argument(
        "--inject_driver_image",
        action="store_true",
        help="Set spec.driver(s).image values to '${driver_image}'.",
    )
    argp.add_argument(
        "--inject_driver_pool",
        action="store_true",
        help="Set spec.driver(s).pool values to '${driver_pool}'.",
    )
    argp.add_argument(
        "--inject_server_pool",
        action="store_true",
        help="Set spec.server(s).pool values to '${server_pool}'.",
    )
    argp.add_argument(
        "--inject_big_query_table",
        action="store_true",
        help="Set spec.results.bigQueryTable to '${big_query_table}'.",
    )
    argp.add_argument(
        "--inject_timeout_seconds",
        action="store_true",
        help="Set spec.timeoutSeconds to '${timeout_seconds}'.",
    )
    argp.add_argument(
        "--inject_ttl_seconds", action="store_true", help="Set timeout "
    )
    argp.add_argument(
        "-n", "--name", default="", type=str, help="metadata.name."
    )
    argp.add_argument(
        "-a",
        "--annotation",
        action="append",
        type=str,
        help="metadata.annotation(s), in the form key=value.",
        dest="annotations",
    )
    args = argp.parse_args()

    annotations = loadtest_config.parse_key_value_args(args.annotations)

    metadata = {"name": args.name}
    if annotations:
        metadata["annotations"] = annotations

    template = loadtest_template(
        input_file_names=args.inputs,
        metadata=metadata,
        inject_client_pool=args.inject_client_pool,
        inject_driver_image=args.inject_driver_image,
        inject_driver_pool=args.inject_driver_pool,
        inject_server_pool=args.inject_server_pool,
        inject_big_query_table=args.inject_big_query_table,
        inject_timeout_seconds=args.inject_timeout_seconds,
        inject_ttl_seconds=args.inject_ttl_seconds,
    )

    with open(args.output, "w") if args.output else sys.stdout as f:
        yaml.dump(
            template,
            stream=f,
            Dumper=template_dumper(TEMPLATE_FILE_HEADER_COMMENT.strip()),
            default_flow_style=False,
        )


if __name__ == "__main__":
    main()
