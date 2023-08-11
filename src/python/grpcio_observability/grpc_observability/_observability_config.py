# Copyright 2023 gRPC authors.
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
"""Helper to read observability config."""

from dataclasses import dataclass
from dataclasses import field
import json
import os
import threading
from typing import Mapping, Optional

GRPC_GCP_OBSERVABILITY_CONFIG_FILE_ENV = "GRPC_GCP_OBSERVABILITY_CONFIG_FILE"
GRPC_GCP_OBSERVABILITY_CONFIG_ENV = "GRPC_GCP_OBSERVABILITY_CONFIG"


@dataclass
class GcpObservabilityConfig:
    _singleton = None
    _lock: threading.RLock = threading.RLock()
    project_id: str = ""
    stats_enabled: bool = False
    tracing_enabled: bool = False
    labels: Optional[Mapping[str, str]] = field(default_factory=dict)
    sampling_rate: Optional[float] = 0.0

    @staticmethod
    def get():
        with GcpObservabilityConfig._lock:
            if GcpObservabilityConfig._singleton is None:
                GcpObservabilityConfig._singleton = GcpObservabilityConfig()
        return GcpObservabilityConfig._singleton

    def load_from_string_content(self, config_contents: str) -> None:
        """Loads the configuration from a string.

        Args:
            config_contents: The configuration string.

        Raises:
            ValueError: If the configuration is invalid.
        """
        try:
            config_json = json.loads(config_contents)
        except json.decoder.JSONDecodeError:
            raise ValueError("Failed to load Json configuration.")

        if config_json and not isinstance(config_json, dict):
            raise ValueError("Found invalid configuration.")

        self.project_id = config_json.get("project_id", "")
        self.labels = config_json.get("labels", {})
        self.stats_enabled = "cloud_monitoring" in config_json.keys()
        self.tracing_enabled = "cloud_trace" in config_json.keys()
        tracing_config = config_json.get("cloud_trace", {})
        self.sampling_rate = tracing_config.get("sampling_rate", 0.0)


def read_config() -> GcpObservabilityConfig:
    """Reads the GCP observability config from the environment variables.

    Returns:
        The GCP observability config.

    Raises:
        ValueError: If the configuration is invalid.
    """
    config_contents = _get_gcp_observability_config_contents()
    config = GcpObservabilityConfig.get()
    config.load_from_string_content(config_contents)

    if not config.project_id:
        # Get project ID from GCP environment variables since project ID was not
        # set it in the GCP observability config.
        config.project_id = _get_gcp_project_id_from_env_var()
        if not config.project_id:
            # Could not find project ID from GCP environment variables either.
            raise ValueError("GCP Project ID not found.")
    return config


def _get_gcp_project_id_from_env_var() -> str:
    """Gets the project ID from the GCP environment variables.

    Returns:
        The project ID, or an empty string if the project ID could not be found.
    """

    project_id = ""
    project_id = os.getenv("GCP_PROJECT")
    if project_id:
        return project_id

    project_id = os.getenv("GCLOUD_PROJECT")
    if project_id:
        return project_id

    project_id = os.getenv("GOOGLE_CLOUD_PROJECT")
    if project_id:
        return project_id

    return project_id  # pytype: disable=bad-return-type


def _get_gcp_observability_config_contents() -> str:
    """Get the contents of the observability config from environment variable or file.

    Returns:
        The content from environment variable, or an empty string if the environment
    variable does not exist.
    """

    contents_str = ""
    # First try get config from GRPC_GCP_OBSERVABILITY_CONFIG_FILE_ENV.
    config_path = os.getenv(GRPC_GCP_OBSERVABILITY_CONFIG_FILE_ENV)
    if config_path:
        with open(config_path, "r") as f:
            contents_str = f.read()

    # Next, try GRPC_GCP_OBSERVABILITY_CONFIG_ENV env var.
    if not contents_str:
        contents_str = os.getenv(GRPC_GCP_OBSERVABILITY_CONFIG_ENV)

    return contents_str  # pytype: disable=bad-return-type
