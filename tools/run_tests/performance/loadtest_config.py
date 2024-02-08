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

# Script to generate test configurations for the OSS benchmarks framework.
#
# This script filters test scenarios and generates uniquely named configurations
# for each test. Configurations are dumped in multipart YAML format.
#
# See documentation below:
# https://github.com/grpc/grpc/blob/master/tools/run_tests/performance/README.md#grpc-oss-benchmarks

import argparse
import collections
import copy
import datetime
import itertools
import json
import os
import string
import sys
from typing import Any, Callable, Dict, Iterable, List, Mapping, Optional, Type

import yaml

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import scenario_config
import scenario_config_exporter

CONFIGURATION_FILE_HEADER_COMMENT = """
# Load test configurations generated from a template by loadtest_config.py.
# See documentation below:
# https://github.com/grpc/grpc/blob/master/tools/run_tests/performance/README.md#grpc-oss-benchmarks
"""


def safe_name(language: str) -> str:
    """Returns a name that is safe to use in labels and file names."""
    return scenario_config.LANGUAGES[language].safename


def default_prefix() -> str:
    """Constructs and returns a default prefix for LoadTest names."""
    return os.environ.get("USER", "loadtest")


def now_string() -> str:
    """Returns the current date and time in string format."""
    return datetime.datetime.now().strftime("%Y%m%d%H%M%S")


def validate_loadtest_name(name: str) -> None:
    """Validates that a LoadTest name is in the expected format."""
    if len(name) > 253:
        raise ValueError(
            "LoadTest name must be less than 253 characters long: %s" % name
        )
    if not all(c.isalnum() and not c.isupper() for c in name if c != "-"):
        raise ValueError("Invalid characters in LoadTest name: %s" % name)
    if not name or not name[0].isalpha() or name[-1] == "-":
        raise ValueError("Invalid format for LoadTest name: %s" % name)


def loadtest_base_name(
    scenario_name: str, uniquifier_elements: Iterable[str]
) -> str:
    """Constructs and returns the base name for a LoadTest resource."""
    name_elements = scenario_name.split("_")
    name_elements.extend(uniquifier_elements)
    return "-".join(element.lower() for element in name_elements)


def loadtest_name(
    prefix: str, scenario_name: str, uniquifier_elements: Iterable[str]
) -> str:
    """Constructs and returns a valid name for a LoadTest resource."""
    base_name = loadtest_base_name(scenario_name, uniquifier_elements)
    name_elements = []
    if prefix:
        name_elements.append(prefix)
    name_elements.append(base_name)
    name = "-".join(name_elements)
    validate_loadtest_name(name)
    return name


def component_name(elements: Iterable[str]) -> str:
    """Constructs a component name from possibly empty elements."""
    return "-".join((e for e in elements if e))


def validate_annotations(annotations: Dict[str, str]) -> None:
    """Validates that annotations do not contain reserved names.

    These names are automatically added by the config generator.
    """
    names = set(("scenario", "uniquifier")).intersection(annotations)
    if names:
        raise ValueError("Annotations contain reserved names: %s" % names)


def gen_run_indices(runs_per_test: int) -> Iterable[str]:
    """Generates run indices for multiple runs, as formatted strings."""
    if runs_per_test < 2:
        yield ""
        return
    index_length = len("{:d}".format(runs_per_test - 1))
    index_fmt = "{{:0{:d}d}}".format(index_length)
    for i in range(runs_per_test):
        yield index_fmt.format(i)


def scenario_name(
    base_name: str,
    client_channels: Optional[int],
    server_threads: Optional[int],
    offered_load: Optional[int],
):
    """Constructs scenario name from base name and modifiers."""

    elements = [base_name]
    if client_channels:
        elements.append("{:d}channels".format(client_channels))
    if server_threads:
        elements.append("{:d}threads".format(server_threads))
    if offered_load:
        elements.append("{:d}load".format(offered_load))
    return "_".join(elements)


def scenario_transform_function(
    client_channels: Optional[int],
    server_threads: Optional[int],
    offered_loads: Optional[Iterable[int]],
) -> Optional[
    Callable[[Iterable[Mapping[str, Any]]], Iterable[Mapping[str, Any]]]
]:
    """Returns a transform to be applied to a list of scenarios."""
    if not any((client_channels, server_threads, len(offered_loads))):
        return lambda s: s

    def _transform(
        scenarios: Iterable[Mapping[str, Any]]
    ) -> Iterable[Mapping[str, Any]]:
        """Transforms scenarios by inserting num of client channels, number of async_server_threads and offered_load."""

        for base_scenario in scenarios:
            base_name = base_scenario["name"]
            if client_channels:
                base_scenario["client_config"][
                    "client_channels"
                ] = client_channels

            if server_threads:
                base_scenario["server_config"][
                    "async_server_threads"
                ] = server_threads

            if not offered_loads:
                base_scenario["name"] = scenario_name(
                    base_name, client_channels, server_threads, 0
                )
                yield base_scenario
                return

            for offered_load in offered_loads:
                scenario = copy.deepcopy(base_scenario)
                scenario["client_config"]["load_params"] = {
                    "poisson": {"offered_load": offered_load}
                }
                scenario["name"] = scenario_name(
                    base_name, client_channels, server_threads, offered_load
                )
                yield scenario

    return _transform


def gen_loadtest_configs(
    base_config: Mapping[str, Any],
    base_config_clients: Iterable[Mapping[str, Any]],
    base_config_servers: Iterable[Mapping[str, Any]],
    scenario_name_regex: str,
    language_config: scenario_config_exporter.LanguageConfig,
    loadtest_name_prefix: str,
    uniquifier_elements: Iterable[str],
    annotations: Mapping[str, str],
    instances_per_client: int = 1,
    runs_per_test: int = 1,
    scenario_transform: Callable[
        [Iterable[Mapping[str, Any]]], List[Dict[str, Any]]
    ] = lambda s: s,
) -> Iterable[Dict[str, Any]]:
    """Generates LoadTest configurations for a given language config.

    The LoadTest configurations are generated as YAML objects.
    """
    validate_annotations(annotations)
    prefix = loadtest_name_prefix or default_prefix()
    cl = safe_name(language_config.client_language or language_config.language)
    sl = safe_name(language_config.server_language or language_config.language)
    scenario_filter = scenario_config_exporter.scenario_filter(
        scenario_name_regex=scenario_name_regex,
        category=language_config.category,
        client_language=language_config.client_language,
        server_language=language_config.server_language,
    )

    scenarios = scenario_transform(
        scenario_config_exporter.gen_scenarios(
            language_config.language, scenario_filter
        )
    )

    for scenario in scenarios:
        for run_index in gen_run_indices(runs_per_test):
            uniq = (
                uniquifier_elements + [run_index]
                if run_index
                else uniquifier_elements
            )
            name = loadtest_name(prefix, scenario["name"], uniq)
            scenario_str = (
                json.dumps({"scenarios": scenario}, indent="  ") + "\n"
            )

            config = copy.deepcopy(base_config)

            metadata = config["metadata"]
            metadata["name"] = name
            if "labels" not in metadata:
                metadata["labels"] = dict()
            metadata["labels"]["language"] = safe_name(language_config.language)
            metadata["labels"]["prefix"] = prefix
            if "annotations" not in metadata:
                metadata["annotations"] = dict()
            metadata["annotations"].update(annotations)
            metadata["annotations"].update(
                {
                    "scenario": scenario["name"],
                    "uniquifier": "-".join(uniq),
                }
            )

            spec = config["spec"]

            # Select clients with the required language.
            clients = [
                client
                for client in base_config_clients
                if client["language"] == cl
            ]
            if not clients:
                raise IndexError(
                    "Client language not found in template: %s" % cl
                )

            # Validate config for additional client instances.
            if instances_per_client > 1:
                c = collections.Counter(
                    (client.get("name", "") for client in clients)
                )
                if max(c.values()) > 1:
                    raise ValueError(
                        "Multiple instances of multiple clients requires "
                        "unique names, name counts for language %s: %s"
                        % (cl, c.most_common())
                    )

            # Name client instances with an index starting from zero.
            client_instances = []
            for i in range(instances_per_client):
                client_instances.extend(copy.deepcopy(clients))
                for client in client_instances[-len(clients) :]:
                    client["name"] = component_name(
                        (client.get("name", ""), str(i))
                    )

            # Set clients to named instances.
            spec["clients"] = client_instances

            # Select servers with the required language.
            servers = copy.deepcopy(
                [
                    server
                    for server in base_config_servers
                    if server["language"] == sl
                ]
            )
            if not servers:
                raise IndexError(
                    "Server language not found in template: %s" % sl
                )

            # Name servers with an index for consistency with clients.
            for i, server in enumerate(servers):
                server["name"] = component_name(
                    (server.get("name", ""), str(i))
                )

            # Set servers to named instances.
            spec["servers"] = servers

            # Add driver, if needed.
            if "driver" not in spec:
                spec["driver"] = dict()

            # Ensure driver has language and run fields.
            driver = spec["driver"]
            if "language" not in driver:
                driver["language"] = safe_name("c++")
            if "run" not in driver:
                driver["run"] = dict()

            # Name the driver with an index for consistency with workers.
            # There is only one driver, so the index is zero.
            if "name" not in driver or not driver["name"]:
                driver["name"] = "0"

            spec["scenariosJSON"] = scenario_str

            yield config


def parse_key_value_args(args: Optional[Iterable[str]]) -> Dict[str, str]:
    """Parses arguments in the form key=value into a dictionary."""
    d = dict()
    if args is None:
        return d
    for arg in args:
        key, equals, value = arg.partition("=")
        if equals != "=":
            raise ValueError("Expected key=value: " + value)
        d[key] = value
    return d


def clear_empty_fields(config: Dict[str, Any]) -> None:
    """Clears fields set to empty values by string substitution."""
    spec = config["spec"]
    if "clients" in spec:
        for client in spec["clients"]:
            if "pool" in client and not client["pool"]:
                del client["pool"]
    if "servers" in spec:
        for server in spec["servers"]:
            if "pool" in server and not server["pool"]:
                del server["pool"]
    if "driver" in spec:
        driver = spec["driver"]
        if "pool" in driver and not driver["pool"]:
            del driver["pool"]
        if (
            "run" in driver
            and "image" in driver["run"]
            and not driver["run"]["image"]
        ):
            del driver["run"]["image"]
    if "results" in spec and not (
        "bigQueryTable" in spec["results"] and spec["results"]["bigQueryTable"]
    ):
        del spec["results"]


def config_dumper(header_comment: str) -> Type[yaml.SafeDumper]:
    """Returns a custom dumper to dump configurations in the expected format."""

    class ConfigDumper(yaml.SafeDumper):
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

    ConfigDumper.add_representer(str, str_presenter)

    return ConfigDumper


def main() -> None:
    language_choices = sorted(scenario_config.LANGUAGES.keys())
    argp = argparse.ArgumentParser(
        description="Generates load test configs from a template.",
        fromfile_prefix_chars="@",
    )
    argp.add_argument(
        "-l",
        "--language",
        action="append",
        choices=language_choices,
        required=True,
        help="Language(s) to benchmark.",
        dest="languages",
    )
    argp.add_argument(
        "-t",
        "--template",
        type=str,
        required=True,
        help="LoadTest configuration yaml file template.",
    )
    argp.add_argument(
        "-s",
        "--substitution",
        action="append",
        default=[],
        help="Template substitution(s), in the form key=value.",
        dest="substitutions",
    )
    argp.add_argument(
        "-p", "--prefix", default="", type=str, help="Test name prefix."
    )
    argp.add_argument(
        "-u",
        "--uniquifier_element",
        action="append",
        default=[],
        help="String element(s) to make the test name unique.",
        dest="uniquifier_elements",
    )
    argp.add_argument(
        "-d",
        action="store_true",
        help="Use creation date and time as an additional uniquifier element.",
    )
    argp.add_argument(
        "-a",
        "--annotation",
        action="append",
        default=[],
        help="metadata.annotation(s), in the form key=value.",
        dest="annotations",
    )
    argp.add_argument(
        "-r",
        "--regex",
        default=".*",
        type=str,
        help="Regex to select scenarios to run.",
    )
    argp.add_argument(
        "--category",
        choices=[
            "all",
            "inproc",
            "scalable",
            "smoketest",
            "sweep",
            "psm",
            "dashboard",
        ],
        default="all",
        help="Select a category of tests to run.",
    )
    argp.add_argument(
        "--allow_client_language",
        action="append",
        choices=language_choices,
        default=[],
        help="Allow cross-language scenarios with this client language.",
        dest="allow_client_languages",
    )
    argp.add_argument(
        "--allow_server_language",
        action="append",
        choices=language_choices,
        default=[],
        help="Allow cross-language scenarios with this server language.",
        dest="allow_server_languages",
    )
    argp.add_argument(
        "--instances_per_client",
        default=1,
        type=int,
        help="Number of instances to generate for each client.",
    )
    argp.add_argument(
        "--runs_per_test",
        default=1,
        type=int,
        help="Number of copies to generate for each test.",
    )
    argp.add_argument(
        "-o",
        "--output",
        type=str,
        help="Output file name. Output to stdout if not set.",
    )
    argp.add_argument(
        "--client_channels", type=int, help="Number of client channels."
    )
    argp.add_argument(
        "--server_threads", type=int, help="Number of async server threads."
    )
    argp.add_argument(
        "--offered_loads",
        nargs="*",
        type=int,
        default=[],
        help=(
            "A list of QPS values at which each load test scenario will be run."
        ),
    )
    args = argp.parse_args()

    if args.instances_per_client < 1:
        argp.error("instances_per_client must be greater than zero.")

    if args.runs_per_test < 1:
        argp.error("runs_per_test must be greater than zero.")

    # Config generation ignores environment variables that are passed by the
    # controller at runtime.
    substitutions = {
        "DRIVER_PORT": "${DRIVER_PORT}",
        "KILL_AFTER": "${KILL_AFTER}",
        "POD_TIMEOUT": "${POD_TIMEOUT}",
    }

    # The user can override the ignored variables above by passing them in as
    # substitution keys.
    substitutions.update(parse_key_value_args(args.substitutions))

    uniquifier_elements = args.uniquifier_elements
    if args.d:
        uniquifier_elements.append(now_string())

    annotations = parse_key_value_args(args.annotations)

    transform = scenario_transform_function(
        args.client_channels, args.server_threads, args.offered_loads
    )

    with open(args.template) as f:
        base_config = yaml.safe_load(
            string.Template(f.read()).substitute(substitutions)
        )

    clear_empty_fields(base_config)

    spec = base_config["spec"]
    base_config_clients = spec["clients"]
    del spec["clients"]
    base_config_servers = spec["servers"]
    del spec["servers"]

    client_languages = [""] + args.allow_client_languages
    server_languages = [""] + args.allow_server_languages
    config_generators = []
    for l, cl, sl in itertools.product(
        args.languages, client_languages, server_languages
    ):
        language_config = scenario_config_exporter.LanguageConfig(
            category=args.category,
            language=l,
            client_language=cl,
            server_language=sl,
        )
        config_generators.append(
            gen_loadtest_configs(
                base_config,
                base_config_clients,
                base_config_servers,
                args.regex,
                language_config,
                loadtest_name_prefix=args.prefix,
                uniquifier_elements=uniquifier_elements,
                annotations=annotations,
                instances_per_client=args.instances_per_client,
                runs_per_test=args.runs_per_test,
                scenario_transform=transform,
            )
        )
    configs = (config for config in itertools.chain(*config_generators))

    with open(args.output, "w") if args.output else sys.stdout as f:
        yaml.dump_all(
            configs,
            stream=f,
            Dumper=config_dumper(CONFIGURATION_FILE_HEADER_COMMENT.strip()),
            default_flow_style=False,
        )


if __name__ == "__main__":
    main()
