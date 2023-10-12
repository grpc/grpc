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

# Library to extract scenario definitions from scenario_config.py.
#
# Contains functions to filter, analyze and dump scenario definitions.
#
# This library is used in loadtest_config.py to generate the "scenariosJSON"
# field in the format accepted by the OSS benchmarks framework.
# See https://github.com/grpc/test-infra/blob/master/config/samples/cxx_example_loadtest.yaml
#
# It can also be used to dump scenarios to files, to count scenarios by
# language, and to export scenario languages in a format that can be used for
# automation.
#
# Example usage:
#
# scenario_config.py --export_scenarios -l cxx -f cxx_scenario_ -r '.*' \
#     --category=scalable
#
# scenario_config.py --count_scenarios
#
# scenario_config.py --count_scenarios --category=scalable
#
# For usage of the language config output, see loadtest_config.py.

import argparse
import collections
import json
import re
import sys
from typing import Any, Callable, Dict, Iterable, NamedTuple

import scenario_config

# Language parameters for load test config generation.

LanguageConfig = NamedTuple(
    "LanguageConfig",
    [
        ("category", str),
        ("language", str),
        ("client_language", str),
        ("server_language", str),
    ],
)


def category_string(categories: Iterable[str], category: str) -> str:
    """Converts a list of categories into a single string for counting."""
    if category != "all":
        return category if category in categories else ""

    main_categories = ("scalable", "smoketest")
    s = set(categories)

    c = [m for m in main_categories if m in s]
    s.difference_update(main_categories)
    c.extend(s)
    return " ".join(c)


def gen_scenario_languages(category: str) -> Iterable[LanguageConfig]:
    """Generates tuples containing the languages specified in each scenario."""
    for language in scenario_config.LANGUAGES:
        for scenario in scenario_config.LANGUAGES[language].scenarios():
            client_language = scenario.get("CLIENT_LANGUAGE", "")
            server_language = scenario.get("SERVER_LANGUAGE", "")
            categories = scenario.get("CATEGORIES", [])
            if category != "all" and category not in categories:
                continue
            cat = category_string(categories, category)
            yield LanguageConfig(
                category=cat,
                language=language,
                client_language=client_language,
                server_language=server_language,
            )


def scenario_filter(
    scenario_name_regex: str = ".*",
    category: str = "all",
    client_language: str = "",
    server_language: str = "",
) -> Callable[[Dict[str, Any]], bool]:
    """Returns a function to filter scenarios to process."""

    def filter_scenario(scenario: Dict[str, Any]) -> bool:
        """Filters scenarios that match specified criteria."""
        if not re.search(scenario_name_regex, scenario["name"]):
            return False
        # if the 'CATEGORIES' key is missing, treat scenario as part of
        # 'scalable' and 'smoketest'. This matches the behavior of
        # run_performance_tests.py.
        scenario_categories = scenario.get(
            "CATEGORIES", ["scalable", "smoketest"]
        )
        if category not in scenario_categories and category != "all":
            return False

        scenario_client_language = scenario.get("CLIENT_LANGUAGE", "")
        if client_language != scenario_client_language:
            return False

        scenario_server_language = scenario.get("SERVER_LANGUAGE", "")
        if server_language != scenario_server_language:
            return False

        return True

    return filter_scenario


def gen_scenarios(
    language_name: str,
    scenario_filter_function: Callable[[Dict[str, Any]], bool],
) -> Iterable[Dict[str, Any]]:
    """Generates scenarios that match a given filter function."""
    return map(
        scenario_config.remove_nonproto_fields,
        filter(
            scenario_filter_function,
            scenario_config.LANGUAGES[language_name].scenarios(),
        ),
    )


def dump_to_json_files(
    scenarios: Iterable[Dict[str, Any]], filename_prefix: str
) -> None:
    """Dumps a list of scenarios to JSON files"""
    count = 0
    for scenario in scenarios:
        filename = "{}{}.json".format(filename_prefix, scenario["name"])
        print("Writing file {}".format(filename), file=sys.stderr)
        with open(filename, "w") as outfile:
            # The dump file should have {"scenarios" : []} as the top level
            # element, when embedded in a LoadTest configuration YAML file.
            json.dump({"scenarios": [scenario]}, outfile, indent=2)
        count += 1
    print("Wrote {} scenarios".format(count), file=sys.stderr)


def main() -> None:
    language_choices = sorted(scenario_config.LANGUAGES.keys())
    argp = argparse.ArgumentParser(description="Exports scenarios to files.")
    argp.add_argument(
        "--export_scenarios",
        action="store_true",
        help="Export scenarios to JSON files.",
    )
    argp.add_argument(
        "--count_scenarios",
        action="store_true",
        help="Count scenarios for all test languages.",
    )
    argp.add_argument(
        "-l", "--language", choices=language_choices, help="Language to export."
    )
    argp.add_argument(
        "-f",
        "--filename_prefix",
        default="scenario_dump_",
        type=str,
        help="Prefix for exported JSON file names.",
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
        default="all",
        choices=[
            "all",
            "inproc",
            "scalable",
            "smoketest",
            "sweep",
            "psm",
            "dashboard",
        ],
        help="Select scenarios for a category of tests.",
    )
    argp.add_argument(
        "--client_language",
        default="",
        choices=language_choices,
        help="Select only scenarios with a specified client language.",
    )
    argp.add_argument(
        "--server_language",
        default="",
        choices=language_choices,
        help="Select only scenarios with a specified server language.",
    )
    args = argp.parse_args()

    if args.export_scenarios and not args.language:
        print(
            "Dumping scenarios requires a specified language.", file=sys.stderr
        )
        argp.print_usage(file=sys.stderr)
        return

    if args.export_scenarios:
        s_filter = scenario_filter(
            scenario_name_regex=args.regex,
            category=args.category,
            client_language=args.client_language,
            server_language=args.server_language,
        )
        scenarios = gen_scenarios(args.language, s_filter)
        dump_to_json_files(scenarios, args.filename_prefix)

    if args.count_scenarios:
        print(
            "Scenario count for all languages (category: {}):".format(
                args.category
            )
        )
        print(
            "{:>5}  {:16} {:8} {:8} {}".format(
                "Count", "Language", "Client", "Server", "Categories"
            )
        )
        c = collections.Counter(gen_scenario_languages(args.category))
        total = 0
        for (cat, l, cl, sl), count in c.most_common():
            print(
                "{count:5}  {l:16} {cl:8} {sl:8} {cat}".format(
                    l=l, cl=cl, sl=sl, count=count, cat=cat
                )
            )
            total += count

        print(
            "\n{:>5}  total scenarios (category: {})".format(
                total, args.category
            )
        )


if __name__ == "__main__":
    main()
