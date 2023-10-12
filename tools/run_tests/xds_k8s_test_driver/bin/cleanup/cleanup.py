# Copyright 2021 gRPC authors.
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
"""Clean up resources created by the tests.

This is intended as a tool to delete leaked resources from old tests.

Typical usage examples:

python3 tools/run_tests/xds_k8s_test_driver/bin/cleanup/cleanup.py\
    --project=grpc-testing\
    --network=default-vpc\
    --kube_context=gke_grpc-testing_us-central1-a_psm-interop-security
    --resource_prefix='required-but-does-not-matter'\
    --td_bootstrap_image='required-but-does-not-matter' --server_image='required-but-does-not-matter' --client_image='required-but-does-not-matter'
"""
import datetime
import functools
import json
import logging
import os
import re
import subprocess
from typing import Any, List

from absl import app
from absl import flags
import dateutil

from framework import xds_flags
from framework import xds_k8s_flags
from framework.infrastructure import gcp
from framework.infrastructure import k8s
from framework.infrastructure import traffic_director
from framework.test_app.runners.k8s import k8s_xds_client_runner
from framework.test_app.runners.k8s import k8s_xds_server_runner

logger = logging.getLogger(__name__)
Json = Any
_KubernetesClientRunner = k8s_xds_client_runner.KubernetesClientRunner
_KubernetesServerRunner = k8s_xds_server_runner.KubernetesServerRunner

GCLOUD = os.environ.get("GCLOUD", "gcloud")
GCLOUD_CMD_TIMEOUT_S = datetime.timedelta(seconds=5).total_seconds()
ZONE = "us-central1-a"
SECONDARY_ZONE = "us-west1-b"

PSM_SECURITY_PREFIX = "psm-interop"  # Prefix for gke resources to delete.
URL_MAP_TEST_PREFIX = (  # Prefix for url-map test resources to delete.
    "interop-psm-url-map"
)

KEEP_PERIOD_HOURS = flags.DEFINE_integer(
    "keep_hours",
    default=168,
    help=(
        "number of hours for a resource to keep. Resources older than this will"
        " be deleted. Default is 168 (7 days)"
    ),
)
DRY_RUN = flags.DEFINE_bool(
    "dry_run",
    default=False,
    help="dry run, print resources but do not perform deletion",
)
TD_RESOURCE_PREFIXES = flags.DEFINE_list(
    "td_resource_prefixes",
    default=[PSM_SECURITY_PREFIX],
    help=(
        "a comma-separated list of prefixes for which the leaked TD resources"
        " will be deleted"
    ),
)
SERVER_PREFIXES = flags.DEFINE_list(
    "server_prefixes",
    default=[PSM_SECURITY_PREFIX],
    help=(
        "a comma-separated list of prefixes for which the leaked servers will"
        " be deleted"
    ),
)
CLIENT_PREFIXES = flags.DEFINE_list(
    "client_prefixes",
    default=[PSM_SECURITY_PREFIX, URL_MAP_TEST_PREFIX],
    help=(
        "a comma-separated list of prefixes for which the leaked clients will"
        " be deleted"
    ),
)


def load_keep_config() -> None:
    global KEEP_CONFIG
    json_path = os.path.realpath(
        os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            "keep_xds_interop_resources.json",
        )
    )
    with open(json_path, "r") as f:
        KEEP_CONFIG = json.load(f)
        logging.debug(
            "Resource keep config loaded: %s", json.dumps(KEEP_CONFIG, indent=2)
        )


def is_marked_as_keep_gce(suffix: str) -> bool:
    return suffix in KEEP_CONFIG["gce_framework"]["suffix"]


def is_marked_as_keep_gke(suffix: str) -> bool:
    return suffix in KEEP_CONFIG["gke_framework"]["suffix"]


@functools.lru_cache()
def get_expire_timestamp() -> datetime.datetime:
    return datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(
        hours=KEEP_PERIOD_HOURS.value
    )


def exec_gcloud(project: str, *cmds: List[str]) -> Json:
    cmds = [GCLOUD, "--project", project, "--quiet"] + list(cmds)
    if "list" in cmds:
        # Add arguments to shape the list output
        cmds.extend(
            [
                "--format",
                "json",
                "--filter",
                f"creationTimestamp <= {get_expire_timestamp().isoformat()}",
            ]
        )
    # Executing the gcloud command
    logging.debug("Executing: %s", " ".join(cmds))
    proc = subprocess.Popen(
        cmds, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    # NOTE(lidiz) the gcloud subprocess won't return unless its output is read
    stdout = proc.stdout.read()
    stderr = proc.stderr.read()
    try:
        returncode = proc.wait(timeout=GCLOUD_CMD_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        logging.error("> Timeout executing cmd [%s]", " ".join(cmds))
        return None
    if returncode:
        logging.error(
            "> Failed to execute cmd [%s], returned %d, stderr: %s",
            " ".join(cmds),
            returncode,
            stderr,
        )
        return None
    if stdout:
        return json.loads(stdout)
    return None


def remove_relative_resources_run_xds_tests(
    project: str, network: str, prefix: str, suffix: str
):
    """Removing GCP resources created by run_xds_tests.py."""
    logging.info(
        "----- Removing run_xds_tests.py resources with suffix [%s]", suffix
    )
    exec_gcloud(
        project,
        "compute",
        "forwarding-rules",
        "delete",
        f"test-forwarding-rule{suffix}",
        "--global",
    )
    exec_gcloud(
        project,
        "compute",
        "target-http-proxies",
        "delete",
        f"test-target-proxy{suffix}",
    )
    exec_gcloud(
        project,
        "alpha",
        "compute",
        "target-grpc-proxies",
        "delete",
        f"test-target-proxy{suffix}",
    )
    exec_gcloud(project, "compute", "url-maps", "delete", f"test-map{suffix}")
    exec_gcloud(
        project,
        "compute",
        "backend-services",
        "delete",
        f"test-backend-service{suffix}",
        "--global",
    )
    exec_gcloud(
        project,
        "compute",
        "backend-services",
        "delete",
        f"test-backend-service-alternate{suffix}",
        "--global",
    )
    exec_gcloud(
        project,
        "compute",
        "backend-services",
        "delete",
        f"test-backend-service-extra{suffix}",
        "--global",
    )
    exec_gcloud(
        project,
        "compute",
        "backend-services",
        "delete",
        f"test-backend-service-more-extra{suffix}",
        "--global",
    )
    exec_gcloud(
        project, "compute", "firewall-rules", "delete", f"test-fw-rule{suffix}"
    )
    exec_gcloud(
        project, "compute", "health-checks", "delete", f"test-hc{suffix}"
    )
    exec_gcloud(
        project,
        "compute",
        "instance-groups",
        "managed",
        "delete",
        f"test-ig{suffix}",
        "--zone",
        ZONE,
    )
    exec_gcloud(
        project,
        "compute",
        "instance-groups",
        "managed",
        "delete",
        f"test-ig-same-zone{suffix}",
        "--zone",
        ZONE,
    )
    exec_gcloud(
        project,
        "compute",
        "instance-groups",
        "managed",
        "delete",
        f"test-ig-secondary-zone{suffix}",
        "--zone",
        SECONDARY_ZONE,
    )
    exec_gcloud(
        project,
        "compute",
        "instance-templates",
        "delete",
        f"test-template{suffix}",
    )


# cleanup_td creates TrafficDirectorManager (and its varients for security and
# AppNet), and then calls the cleanup() methods.
#
# Note that the varients are all based on the basic TrafficDirectorManager, so
# their `cleanup()` might do duplicate work. But deleting an non-exist resource
# returns 404, and is OK.
def cleanup_td_for_gke(project, network, resource_prefix, resource_suffix):
    gcp_api_manager = gcp.api.GcpApiManager()
    plain_td = traffic_director.TrafficDirectorManager(
        gcp_api_manager,
        project=project,
        network=network,
        resource_prefix=resource_prefix,
        resource_suffix=resource_suffix,
    )
    security_td = traffic_director.TrafficDirectorSecureManager(
        gcp_api_manager,
        project=project,
        network=network,
        resource_prefix=resource_prefix,
        resource_suffix=resource_suffix,
    )
    # TODO: cleanup appnet resources.
    # appnet_td = traffic_director.TrafficDirectorAppNetManager(
    #     gcp_api_manager,
    #     project=project,
    #     network=network,
    #     resource_prefix=resource_prefix,
    #     resource_suffix=resource_suffix)

    logger.info(
        "----- Removing traffic director for gke, prefix %s, suffix %s",
        resource_prefix,
        resource_suffix,
    )
    security_td.cleanup(force=True)
    # appnet_td.cleanup(force=True)
    plain_td.cleanup(force=True)


# cleanup_client creates a client runner, and calls its cleanup() method.
def cleanup_client(
    project,
    network,
    k8s_api_manager,
    resource_prefix,
    resource_suffix,
    gcp_service_account,
):
    runner_kwargs = dict(
        deployment_name=xds_flags.CLIENT_NAME.value,
        image_name=xds_k8s_flags.CLIENT_IMAGE.value,
        td_bootstrap_image=xds_k8s_flags.TD_BOOTSTRAP_IMAGE.value,
        gcp_project=project,
        gcp_api_manager=gcp.api.GcpApiManager(),
        gcp_service_account=gcp_service_account,
        xds_server_uri=xds_flags.XDS_SERVER_URI.value,
        network=network,
        stats_port=xds_flags.CLIENT_PORT.value,
    )

    client_namespace = _KubernetesClientRunner.make_namespace_name(
        resource_prefix, resource_suffix
    )
    client_runner = _KubernetesClientRunner(
        k8s.KubernetesNamespace(k8s_api_manager, client_namespace),
        **runner_kwargs,
    )

    logger.info("Cleanup client")
    client_runner.cleanup(force=True, force_namespace=True)


# cleanup_server creates a server runner, and calls its cleanup() method.
def cleanup_server(
    project,
    network,
    k8s_api_manager,
    resource_prefix,
    resource_suffix,
    gcp_service_account,
):
    runner_kwargs = dict(
        deployment_name=xds_flags.SERVER_NAME.value,
        image_name=xds_k8s_flags.SERVER_IMAGE.value,
        td_bootstrap_image=xds_k8s_flags.TD_BOOTSTRAP_IMAGE.value,
        gcp_project=project,
        gcp_api_manager=gcp.api.GcpApiManager(),
        gcp_service_account=gcp_service_account,
        network=network,
    )

    server_namespace = _KubernetesServerRunner.make_namespace_name(
        resource_prefix, resource_suffix
    )
    server_runner = _KubernetesServerRunner(
        k8s.KubernetesNamespace(k8s_api_manager, server_namespace),
        **runner_kwargs,
    )

    logger.info("Cleanup server")
    server_runner.cleanup(force=True, force_namespace=True)


def delete_leaked_td_resources(
    dry_run, td_resource_rules, project, network, resources
):
    for resource in resources:
        logger.info("-----")
        logger.info("----- Cleaning up resource %s", resource["name"])
        if dry_run:
            # Skip deletion for dry-runs
            logging.info("----- Skipped [Dry Run]: %s", resource["name"])
            continue
        matched = False
        for regex, resource_prefix, keep, remove in td_resource_rules:
            result = re.search(regex, resource["name"])
            if result is not None:
                matched = True
                if keep(result.group(1)):
                    logging.info("Skipped [keep]:")
                    break  # break inner loop, continue outer loop
                remove(project, network, resource_prefix, result.group(1))
                break
        if not matched:
            logging.info(
                "----- Skipped [does not matching resource name templates]"
            )


def delete_k8s_resources(
    dry_run,
    k8s_resource_rules,
    project,
    network,
    k8s_api_manager,
    gcp_service_account,
    namespaces,
):
    for ns in namespaces:
        logger.info("-----")
        logger.info("----- Cleaning up k8s namespaces %s", ns.metadata.name)
        if ns.metadata.creation_timestamp <= get_expire_timestamp():
            if dry_run:
                # Skip deletion for dry-runs
                logging.info("----- Skipped [Dry Run]: %s", ns.metadata.name)
                continue

            matched = False
            for regex, resource_prefix, remove in k8s_resource_rules:
                result = re.search(regex, ns.metadata.name)
                if result is not None:
                    matched = True
                    remove(
                        project,
                        network,
                        k8s_api_manager,
                        resource_prefix,
                        result.group(1),
                        gcp_service_account,
                    )
                    break
            if not matched:
                logging.info(
                    "----- Skipped [does not matching resource name templates]"
                )
        else:
            logging.info("----- Skipped [resource is within expiry date]")


def find_and_remove_leaked_k8s_resources(
    dry_run, project, network, gcp_service_account
):
    k8s_resource_rules = [
        # items in each tuple, in order
        # - regex to match
        # - prefix of the resources
        # - function to delete the resource
    ]
    for prefix in CLIENT_PREFIXES.value:
        k8s_resource_rules.append(
            (f"{prefix}-client-(.*)", prefix, cleanup_client),
        )
    for prefix in SERVER_PREFIXES.value:
        k8s_resource_rules.append(
            (f"{prefix}-server-(.*)", prefix, cleanup_server),
        )

    # Delete leaked k8s namespaces, those usually mean there are leaked testing
    # client/servers from the gke framework.
    k8s_api_manager = k8s.KubernetesApiManager(xds_k8s_flags.KUBE_CONTEXT.value)
    nss = k8s_api_manager.core.list_namespace()
    delete_k8s_resources(
        dry_run,
        k8s_resource_rules,
        project,
        network,
        k8s_api_manager,
        gcp_service_account,
        nss.items,
    )


def main(argv):
    if len(argv) > 1:
        raise app.UsageError("Too many command-line arguments.")
    load_keep_config()

    # Must be called before KubernetesApiManager or GcpApiManager init.
    xds_flags.set_socket_default_timeout_from_flag()

    project: str = xds_flags.PROJECT.value
    network: str = xds_flags.NETWORK.value
    gcp_service_account: str = xds_k8s_flags.GCP_SERVICE_ACCOUNT.value
    dry_run: bool = DRY_RUN.value

    td_resource_rules = [
        # itmes in each tuple, in order
        # - regex to match
        # - prefix of the resource (only used by gke resources)
        # - function to check of the resource should be kept
        # - function to delete the resource
        (
            r"test-hc(.*)",
            "",
            is_marked_as_keep_gce,
            remove_relative_resources_run_xds_tests,
        ),
        (
            r"test-template(.*)",
            "",
            is_marked_as_keep_gce,
            remove_relative_resources_run_xds_tests,
        ),
    ]
    for prefix in TD_RESOURCE_PREFIXES.value:
        td_resource_rules.append(
            (
                f"{prefix}-health-check-(.*)",
                prefix,
                is_marked_as_keep_gke,
                cleanup_td_for_gke,
            ),
        )

    # List resources older than KEEP_PERIOD. We only list health-checks and
    # instance templates because these are leaves in the resource dependency tree.
    #
    # E.g. forwarding-rule depends on the target-proxy. So leaked
    # forwarding-rule indicates there's a leaked target-proxy (because this
    # target proxy cannot deleted unless the forwarding rule is deleted). The
    # leaked target-proxy is guaranteed to be a super set of leaked
    # forwarding-rule.
    compute = gcp.compute.ComputeV1(gcp.api.GcpApiManager(), project)
    leakedHealthChecks = []
    for item in compute.list_health_check()["items"]:
        if (
            dateutil.parser.isoparse(item["creationTimestamp"])
            <= get_expire_timestamp()
        ):
            leakedHealthChecks.append(item)

    delete_leaked_td_resources(
        dry_run, td_resource_rules, project, network, leakedHealthChecks
    )

    # Delete leaked instance templates, those usually mean there are leaked VMs
    # from the gce framework. Also note that this is only needed for the gce
    # resources.
    leakedInstanceTemplates = exec_gcloud(
        project, "compute", "instance-templates", "list"
    )
    delete_leaked_td_resources(
        dry_run, td_resource_rules, project, network, leakedInstanceTemplates
    )

    find_and_remove_leaked_k8s_resources(
        dry_run, project, network, gcp_service_account
    )


if __name__ == "__main__":
    app.run(main)
