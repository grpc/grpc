# Copyright 2024 gRPC authors.
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

from typing import Iterable, Optional

from grpc_csm_observability._csm_observability_plugin import (
    CsmOpenTelemetryPlugin,
)
from grpc_csm_observability._csm_observability_plugin import (
    _CsmOpenTelemetryPlugin,
)
from grpc_observability import OpenTelemetryObservability
from grpc_observability import _open_telemetry_observability

# pytype: disable=pyi-error
from grpc_observability._open_telemetry_plugin import _OpenTelemetryPlugin


def start_csm_observability(
    *,
    plugins: Optional[Iterable[CsmOpenTelemetryPlugin]] = None
) -> None:
    csm_o11y = CsmObservability(
        plugins=plugins
    )
    _open_telemetry_observability.init_open_telemetry_observability(csm_o11y)


def end_csm_observability() -> None:
    _open_telemetry_observability.deinit_open_telemetry_observability()


# pylint: disable=no-self-use
class CsmObservability(OpenTelemetryObservability):
    """OpenTelemetry based plugin implementation for CSM.

    This is class is part of an EXPERIMENTAL API.

    Args:
      plugin: CsmOpenTelemetryPlugin to enable.
    """

    _exporter: "grpc_observability.Exporter"

    def __init__(
        self,
        *,
        plugins: Optional[Iterable[CsmOpenTelemetryPlugin]] = None,
    ):
        self._exporter = _open_telemetry_observability._OpenTelemetryExporterDelegator(plugins)
        self._registered_methods = set()

    def is_server_traced(self, xds: bool) -> bool:
        return xds
