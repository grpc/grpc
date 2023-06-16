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

python3 -m bin.cleanup.cleanup \
    --project=grpc-testing \
    --network=default-vpc \
    --kube_context=gke_grpc-testing_us-central1-a_psm-interop-security
"""
import dataclasses
import datetime
import functools
import json
import logging
import os
import re
import subprocess
import sys
from typing import Any, Callable, List, Optional

from absl import app
from absl import flags
import dateutil

from framework import xds_flags
from framework import xds_k8s_flags
from framework.helpers import retryers
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

# Skip known k8s system namespaces.
K8S_PROTECTED_NAMESPACES = {
    "default",
    "gke-managed-system",
    "kube-node-lease",
    "kube-public",
    "kube-system",
}

# TODO(sergiitk): these should be flags.
LEGACY_DRIVER_ZONE = "us-central1-a"
LEGACY_DRIVER_SECONDARY_ZONE = "us-west1-b"

PSM_INTEROP_PREFIX = "psm-interop"  # Prefix for gke resources to delete.
URL_MAP_TEST_PREFIX = (
    "interop-psm-url-map"  # Prefix for url-map test resources to delete.
)

KEEP_PERIOD_HOURS = flags.DEFINE_integer(
    "keep_hours",
    default=168,
    help="number of hours for a resource to keep. Resources older than this will be deleted. Default is 168 (7 days)",
)
DRY_RUN = flags.DEFINE_bool(
    "dry_run",
    default=False,
    help="dry run, print resources but do not perform deletion",
)
TD_RESOURCE_PREFIXES = flags.DEFINE_list(
    "td_resource_prefixes",
    default=[PSM_INTEROP_PREFIX],
    help="a comma-separated list of `prefixes for which the leaked TD resources will be deleted",
)
SERVER_PREFIXES = flags.DEFINE_list(
    "server_prefixes",
    default=[PSM_INTEROP_PREFIX],
    help="a comma-separated list of prefixes for which the leaked servers will be deleted",
)
CLIENT_PREFIXES = flags.DEFINE_list(
    "client_prefixes",
    default=[PSM_INTEROP_PREFIX, URL_MAP_TEST_PREFIX],
    help="a comma-separated list of prefixes for which the leaked clients will be deleted",
)
MODE = flags.DEFINE_enum(
    "mode",
    default="td",
    enum_values=["k8s", "td", "td_no_legacy"],
    help="Mode: Kubernetes or Traffic Director",
)
SECONDARY = flags.DEFINE_bool(
    "secondary", default=False, help="Cleanup secondary (alternative) resources"
)

# The cleanup script performs some API calls directly, so some flags normally
# required to configure framework properly, are not needed here.
flags.FLAGS.set_default("resource_prefix", "ignored-by-cleanup")
flags.FLAGS.set_default("td_bootstrap_image", "ignored-by-cleanup")
flags.FLAGS.set_default("server_image", "ignored-by-cleanup")
flags.FLAGS.set_default("client_image", "ignored-by-cleanup")


@dataclasses.dataclass(eq=False)
class CleanupResult:
    error_count: int = 0
    error_messages: List[str] = dataclasses.field(default_factory=list)

    def add_error(self, msg: str):
        self.error_count += 1
        self.error_messages.append(f"  {self.error_count}. {msg}")

    def format_messages(self):
        return "\n".join(self.error_messages)


@dataclasses.dataclass(frozen=True)
class K8sResourceRule:
    # regex to match
    expression: str
    # function to delete the resource
    cleanup_ns_fn: Callable


# Global state, holding the result of the whole operation.
_CLEANUP_RESULT = CleanupResult()


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


def exec_gcloud(project: str, *cmds: str) -> Json:
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


def cleanup_legacy_driver_resources(*, project: str, suffix: str, **kwargs):
    """Removing GCP resources created by run_xds_tests.py."""
    # Unused, but kept for compatibility with cleanup_td_for_gke.
    del kwargs
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
        LEGACY_DRIVER_ZONE,
    )
    exec_gcloud(
        project,
        "compute",
        "instance-groups",
        "managed",
        "delete",
        f"test-ig-same-zone{suffix}",
        "--zone",
        LEGACY_DRIVER_ZONE,
    )
    exec_gcloud(
        project,
        "compute",
        "instance-groups",
        "managed",
        "delete",
        f"test-ig-secondary-zone{suffix}",
        "--zone",
        LEGACY_DRIVER_SECONDARY_ZONE,
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
def cleanup_td_for_gke(*, project, prefix, suffix, network):
    gcp_api_manager = gcp.api.GcpApiManager()
    plain_td = traffic_director.TrafficDirectorManager(
        gcp_api_manager,
        project=project,
        network=network,
        resource_prefix=prefix,
        resource_suffix=suffix,
    )
    security_td = traffic_director.TrafficDirectorSecureManager(
        gcp_api_manager,
        project=project,
        network=network,
        resource_prefix=prefix,
        resource_suffix=suffix,
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
        prefix,
        suffix,
    )
    security_td.cleanup(force=True)
    # appnet_td.cleanup(force=True)
    plain_td.cleanup(force=True)


# cleanup_client creates a client runner, and calls its cleanup() method.
def cleanup_client(
    project,
    network,
    k8s_api_manager,
    client_namespace,
    gcp_api_manager,
    gcp_service_account,
    *,
    suffix: Optional[str] = "",
):
    deployment_name = xds_flags.CLIENT_NAME.value
    if suffix:
        deployment_name = f"{deployment_name}-{suffix}"

    ns = k8s.KubernetesNamespace(k8s_api_manager, client_namespace)
    # Shorten the timeout to avoid waiting for the stuck namespaces.
    # Normal ns deletion during the cleanup takes less two minutes.
    ns.wait_for_namespace_deleted_timeout_sec = 5 * 60
    client_runner = _KubernetesClientRunner(
        k8s_namespace=ns,
        deployment_name=deployment_name,
        gcp_project=project,
        network=network,
        gcp_service_account=gcp_service_account,
        gcp_api_manager=gcp_api_manager,
        image_name="",
        td_bootstrap_image="",
    )

    logger.info("Cleanup client")
    try:
        client_runner.cleanup(force=True, force_namespace=True)
    except retryers.RetryError as err:
        logger.error(
            "Timeout waiting for namespace %s deletion. "
            "Failed resource status:\n\n%s",
            ns.name,
            ns.pretty_format_status(err.result()),
        )
        raise


# cleanup_server creates a server runner, and calls its cleanup() method.
def cleanup_server(
    project,
    network,
    k8s_api_manager,
    server_namespace,
    gcp_api_manager,
    gcp_service_account,
    *,
    suffix: Optional[str] = "",
):
    deployment_name = xds_flags.SERVER_NAME.value
    if suffix:
        deployment_name = f"{deployment_name}-{suffix}"

    ns = k8s.KubernetesNamespace(k8s_api_manager, server_namespace)
    # Shorten the timeout to avoid waiting for the stuck namespaces.
    # Normal ns deletion during the cleanup takes less two minutes.
    ns.wait_for_namespace_deleted_timeout_sec = 5 * 60
    server_runner = _KubernetesServerRunner(
        k8s_namespace=ns,
        deployment_name=deployment_name,
        gcp_project=project,
        network=network,
        gcp_service_account=gcp_service_account,
        gcp_api_manager=gcp_api_manager,
        image_name="",
        td_bootstrap_image="",
    )

    logger.info("Cleanup server")
    try:
        server_runner.cleanup(force=True, force_namespace=True)
    except retryers.RetryError as err:
        logger.error(
            "Timeout waiting for namespace %s deletion. "
            "Failed resource status:\n\n%s",
            ns.name,
            ns.pretty_format_status(err.result()),
        )
        raise


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
        for regex, resource_prefix, keep, remove_fn in td_resource_rules:
            result = re.search(regex, resource["name"])
            if result is not None:
                matched = True
                if keep(result.group(1)):
                    logging.info("Skipped [keep]:")
                    break  # break inner loop, continue outer loop
                remove_fn(
                    project=project,
                    prefix=resource_prefix,
                    suffix=result.group(1),
                    network=network,
                )
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
    gcp_api_manager = gcp.api.GcpApiManager()
    for ns in namespaces:
        namespace_name: str = ns.metadata.name
        if namespace_name in K8S_PROTECTED_NAMESPACES:
            continue

        logger.info("-----")
        logger.info("----- Cleaning up k8s namespaces %s", namespace_name)

        if ns.metadata.creation_timestamp > get_expire_timestamp():
            logging.info(
                "----- Skipped [resource is within expiry date]: %s",
                namespace_name,
            )
            continue

        if dry_run:
            # Skip deletion for dry-runs
            logging.info("----- Skipped [Dry Run]: %s", ns.metadata.name)
            continue

        rule: K8sResourceRule = _rule_match_k8s_namespace(
            namespace_name, k8s_resource_rules
        )
        if not rule:
            logging.info(
                "----- Skipped [does not matching resource name templates]: %s",
                namespace_name,
            )
            continue

        # Cleaning up.
        try:
            rule.cleanup_ns_fn(
                project,
                network,
                k8s_api_manager,
                namespace_name,
                gcp_api_manager,
                gcp_service_account,
                suffix=("alt" if SECONDARY.value else None),
            )
        except k8s.NotFound:
            logging.warning("----- Skipped [not found]: %s", namespace_name)
        except retryers.RetryError as err:
            _CLEANUP_RESULT.add_error(
                "Retries exhausted while waiting for the "
                f"deletion of namespace {namespace_name}: "
                f"{err}"
            )
            logging.exception(
                "----- Skipped [cleanup timed out]: %s", namespace_name
            )
        except Exception as err:  # noqa pylint: disable=broad-except
            _CLEANUP_RESULT.add_error(
                "Unexpected error while deleting "
                f"namespace {namespace_name}: {err}"
            )
            logging.exception(
                "----- Skipped [cleanup unexpected error]: %s", namespace_name
            )

    logger.info("-----")


def _rule_match_k8s_namespace(
    namespace_name: str, k8s_resource_rules: List[K8sResourceRule]
) -> Optional[K8sResourceRule]:
    for rule in k8s_resource_rules:
        result = re.search(rule.expression, namespace_name)
        if result is not None:
            return rule
    return None


def find_and_remove_leaked_k8s_resources(
    dry_run, project, network, gcp_service_account, k8s_context
):
    k8s_resource_rules: List[K8sResourceRule] = []
    for prefix in CLIENT_PREFIXES.value:
        k8s_resource_rules.append(
            K8sResourceRule(f"{prefix}-client-(.*)", cleanup_client)
        )
    for prefix in SERVER_PREFIXES.value:
        k8s_resource_rules.append(
            K8sResourceRule(f"{prefix}-server-(.*)", cleanup_server)
        )

    # Delete leaked k8s namespaces, those usually mean there are leaked testing
    # client/servers from the gke framework.
    k8s_api_manager = k8s.KubernetesApiManager(k8s_context)
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


def find_and_remove_leaked_td_resources(dry_run, project, network):
    cleanup_legacy: bool = MODE.value != "td_no_legacy"
    td_resource_rules = [
        # itmes in each tuple, in order
        # - regex to match
        # - prefix of the resource (only used by gke resources)
        # - function to check of the resource should be kept
        # - function to delete the resource
    ]

    if cleanup_legacy:
        td_resource_rules += [
            (
                r"test-hc(.*)",
                "",
                is_marked_as_keep_gce,
                cleanup_legacy_driver_resources,
            ),
            (
                r"test-template(.*)",
                "",
                is_marked_as_keep_gce,
                cleanup_legacy_driver_resources,
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
    # instance templates because these are leaves in the resource dependency
    # tree.
    #
    # E.g. forwarding-rule depends on the target-proxy. So leaked
    # forwarding-rule indicates there's a leaked target-proxy (because this
    # target proxy cannot deleted unless the forwarding rule is deleted). The
    # leaked target-proxy is guaranteed to be a super set of leaked
    # forwarding-rule.
    compute = gcp.compute.ComputeV1(gcp.api.GcpApiManager(), project)
    leaked_health_checks = []
    for item in compute.list_health_check()["items"]:
        if (
            dateutil.parser.isoparse(item["creationTimestamp"])
            <= get_expire_timestamp()
        ):
            leaked_health_checks.append(item)

    delete_leaked_td_resources(
        dry_run, td_resource_rules, project, network, leaked_health_checks
    )

    # Delete leaked instance templates, those usually mean there are leaked VMs
    # from the gce framework. Also note that this is only needed for the gce
    # resources.
    if cleanup_legacy:
        leaked_instance_templates = exec_gcloud(
            project, "compute", "instance-templates", "list"
        )
        delete_leaked_td_resources(
            dry_run,
            td_resource_rules,
            project,
            network,
            leaked_instance_templates,
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
    k8s_context: str = xds_k8s_flags.KUBE_CONTEXT.value

    if MODE.value == "td" or MODE.value == "td_no_legacy":
        find_and_remove_leaked_td_resources(dry_run, project, network)
    elif MODE.value == "k8s":
        # 'unset' value is used in td-only mode to bypass the validation
        # for the required  flag.
        assert k8s_context != "unset"
        find_and_remove_leaked_k8s_resources(
            dry_run, project, network, gcp_service_account, k8s_context
        )

    logger.info("##################### Done cleaning up #####################")
    if _CLEANUP_RESULT.error_count > 0:
        logger.error(
            "Cleanup failed for %i resource(s). Errors: [\n%s\n].\n"
            "Please inspect the log files for stack traces corresponding "
            "to these errors.",
            _CLEANUP_RESULT.error_count,
            _CLEANUP_RESULT.format_messages(),
        )
        sys.exit(1)


if __name__ == "__main__":
    app.run(main)
