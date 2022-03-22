#!/usr/bin/env python3

# Copyright 2022 The gRPC Authors
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


# example usage: python3 prometheus.py --url=http://10.108.4.94:9090
# --pod_type=driver --pod_type=client --container_name=main
# --container_name=sidecar 
import argparse
import json
import logging
import requests
import statistics

from dateutil import parser
from typing import Any, Dict, List


class Prometheus:
    def __init__(
        self,
        url: str,
        start: str,
        end: str,
    ):
        self.url = url
        self.start = start
        self.end = end

    def _fetch_by_query(self, query: str) -> Any:
        """Fetches the given query with time range."""
        resp = requests.get(
            self.url + "/api/v1/query_range",
            {"query": query, "start": self.start, "end": self.end, "step": 5},
        )
        resp.raise_for_status()
        return resp.json()

    def _fetch_cpu_for_pod(
        self, container_matcher: str, pod_name: str
    ) -> Dict[str, List[float]]:
        """Fetches the cpu data for each pod and construct the container
        name to cpu data list Dict."""
        query = (
            'container_cpu_usage_seconds_total{job="kubernetes-cadvisor",pod="'
            + pod_name
            + '",container='
            + container_matcher
            + "}"
        )
        logging.debug("running prometheus query for cpu:" + query)
        cpu_data = self._fetch_by_query(query)
        logging.debug("raw cpu data:" + str(cpu_data))
        cpu_container_name_to_data_list = get_data_list_from_timeseries(cpu_data)
        return cpu_container_name_to_data_list

    def _fetch_memory_for_pod(
        self, container_matcher: str, pod_name: str
    ) -> Dict[str, List[float]]:
        """Fetches the memory data for each pod and construct the
        container name to memory data list Dict."""
        query = (
            'container_memory_usage_bytes{job="kubernetes-cadvisor",pod="'
            + pod_name
            + '",container='
            + container_matcher
            + "}"
        )

        logging.debug("running prometheus query for memory:" + query)
        memory_data = self._fetch_by_query(query)

        logging.debug("raw memory data:" + str(memory_data))
        memory_container_name_to_data_list = get_data_list_from_timeseries(memory_data)

        return memory_container_name_to_data_list

    def fetch_cpu_and_memory_data(
        self, container_list: List[str], pod_list: List[str]) -> Dict[str, Any]:
        """Fetches and process min, max, mean, std for the memory and cpu
        data for each container in the container_list for each pod in
        the pod_list and construct processed data group first by metric
        type (cpu or memory) and then by pod name and container name.
        If a given container does not exit on a pod, it is ignored."""
        container_matcher = construct_container_matcher(container_list)
        processed_data = {}

        raw_cpu_data = {}
        raw_memory_data = {}
        for pod in pod_list:
            raw_cpu_data[pod] = self._fetch_cpu_for_pod(container_matcher, pod)
            raw_memory_data[pod] = self._fetch_memory_for_pod(container_matcher, pod)

        processed_data["cpu"] = compute_total_cpu_seconds(raw_cpu_data)
        processed_data["memory"] = compute_average_memory_usage(raw_memory_data)
        return processed_data


def construct_container_matcher(container_list: List[str]) -> str:
    """Constructs the container matching string used in the
    prometheus query to match container names based on given
    list of the containers."""
    if len(container_list) == 0:
        raise Exception("no container name provided")

    containers_to_fetch = '"'
    if len(container_list) == 1:
        containers_to_fetch = container_list[0]
    else:
        containers_to_fetch = '~"' + container_list[0]
        for container in container_list[1:]:
            containers_to_fetch = containers_to_fetch + "|" + container
            containers_to_fetch = containers_to_fetch + '"'
    return containers_to_fetch


def get_data_list_from_timeseries(data: Any) -> Dict[str, List[float]]:
    """Constructs a Dict as keys are the container names and
    values are a list of data taken from given timeserie data."""
    if data["status"] != "success":
        raise Exception("command failed: " + data["status"] + str(data))
    if data["data"]["resultType"] != "matrix":
        raise Exception("resultType is not matrix: " + data["data"]["resultType"])

    container_name_to_data_list = {}
    for res in data["data"]["result"]:
        container_name = res["metric"]["container"]
        container_data_timeserie = res["values"]

        container_data = []
        for d in container_data_timeserie:
            container_data.append(float(d[1]))
        container_name_to_data_list[container_name] = container_data
    return container_name_to_data_list



def compute_total_cpu_seconds(
    cpu_data_dicts: Dict[str, Dict[str, List[float]]]
) -> Dict[str, Dict[str,float]]:
    """Computes the total cpu seconds by CPUs[end]-CPUs[start]."""
    pod_name_to_data_dicts = {}
    for pod_name, pod_data_dicts in cpu_data_dicts.items():
        container_name_to_processed_data = {}
        for container_name, data_list in pod_data_dicts.items():
            container_name_to_processed_data[
                container_name
            ] = float(data_list[len(data_list)-1]-data_list[0])
        pod_name_to_data_dicts[pod_name] = container_name_to_processed_data

    return pod_name_to_data_dicts

def compute_average_memory_usage(
    memory_data_dicts: Dict[str, Dict[str, List[float]]]
) -> Dict[str, Dict[str, float]]:
    """Computes the min, max, mean and standard deviation for
    given set of data."""
    pod_name_to_data_dicts = {}
    for pod_name, pod_data_dicts in memory_data_dicts.items():
        container_name_to_processed_data = {}
        for container_name, data_list in pod_data_dicts.items():
            container_name_to_processed_data[
                container_name
            ] = statistics.mean(data_list)
        pod_name_to_data_dicts[pod_name] = container_name_to_processed_data

    return pod_name_to_data_dicts


def construct_pod_list(node_info_file: str, pod_types: List[str]) -> List[str]:
    """Constructs a list of pod names to be queried."""
    with open(node_info_file, "r") as f:
        pod_names = json.load(f)
        pod_type_to_name = {"client": [], "driver": [], "server": []}

        for client in pod_names["Clients"]:
            pod_type_to_name["client"].append(client["Name"])
        for server in pod_names["Servers"]:
            pod_type_to_name["server"].append(server["Name"])

        pod_type_to_name["driver"].append(pod_names["Driver"]["Name"])

    pod_names_to_query = []
    for pod_type in pod_types:
        for each in pod_type_to_name[pod_type]:
            pod_names_to_query.append(each)
    return pod_names_to_query


def convert_UTC_to_epoch(utc_timestamp: str) -> str:
    """Converts a utc timstamp string to epoch time string."""
    parsed_time = parser.parse(utc_timestamp)
    epoch = parsed_time.strftime("%s")
    return epoch


def main() -> None:
    argp = argparse.ArgumentParser(
        description="Fetch cpu and memory stats from prometheus"
    )
    argp.add_argument("--url", help="Prometheus base url", required=True)
    argp.add_argument(
        "--scenario_result_file",
        default="scenario_result.json",
        type=str,
        help="File contains epoch seconds for start and end time",
    )
    argp.add_argument(
        "--node_info_file",
        default="/var/data/qps_workers/node_info.json",
        help="File contains pod name to query the metrics for",
    )
    argp.add_argument(
        "--pod_type",
        action="append",
        help="Pod type to query the metrics for, the options are driver, client and server",
        choices=["driver", "client", "server"],
        required=True,
    )
    argp.add_argument(
        "--container_name",
        action="append",
        help="The container names to query the metrics for",
        required=True,
    )
    argp.add_argument(
        "--export_file_name",
        default="prometheus_query_result.json",
        type=str,
        help="Name of exported JSON file.",
    )
    argp.add_argument(
        "--quiet",
        default=False,
        help="Suppress informative output",
    )
    args = argp.parse_args()

    if not args.quiet:
        logging.getLogger().setLevel(logging.DEBUG)

    with open(args.scenario_result_file, "r") as q:
        scenario_result = json.load(q)
        start_time = convert_UTC_to_epoch(scenario_result["summary"]["startTime"])
        end_time = convert_UTC_to_epoch(scenario_result["summary"]["endTime"])
        p = Prometheus(
            url=args.url,
            start=start_time,
            end=end_time,
        )

    test_duration_seconds = float(end_time) - float(start_time)
    pod_list = construct_pod_list(args.node_info_file, args.pod_type)
    processed_data = p.fetch_cpu_and_memory_data(
        container_list=args.container_name, pod_list=pod_list
    )
    processed_data["testDurationSeconds"]=test_duration_seconds

    logging.debug(json.dumps(processed_data, sort_keys=True, indent=4))

    with open(args.export_file_name, "w") as export_file:
        json.dump(processed_data, export_file, sort_keys=True, indent=4)


if __name__ == "__main__":
    main()
