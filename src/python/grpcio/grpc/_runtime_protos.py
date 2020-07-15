# Copyright 2020 The gRPC authors.
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

import sys


def _uninstalled_protos(*args, **kwargs):
    raise NotImplementedError(
        "Install the grpcio-tools package to use the protos function.")


def _uninstalled_services(*args, **kwargs):
    raise NotImplementedError(
        "Install the grpcio-tools package to use the services function.")


def _uninstalled_protos_and_services(*args, **kwargs):
    raise NotImplementedError(
        "Install the grpcio-tools package to use the protos_and_services function."
    )


def _interpreter_version_protos(*args, **kwargs):
    raise NotImplementedError(
        "The protos function is only on available on Python 3.X interpreters.")


def _interpreter_version_services(*args, **kwargs):
    raise NotImplementedError(
        "The services function is only on available on Python 3.X interpreters."
    )


def _interpreter_version_protos_and_services(*args, **kwargs):
    raise NotImplementedError(
        "The protos_and_services function is only on available on Python 3.X interpreters."
    )


if sys.version_info[0] < 3:
    protos = _interpreter_version_protos
    services = _interpreter_version_services
    protos_and_services = _interpreter_version_protos_and_services
else:
    try:
        import grpc_tools
    except ImportError as e:
        # NOTE: It's possible that we're encountering a transitive ImportError, so
        # we check for that and re-raise if so.
        if "grpc_tools" not in e.args[0]:
            raise
        protos = _uninstalled_protos
        services = _uninstalled_services
        protos_and_services = _uninstalled_protos_and_services
    else:
        from grpc_tools.protoc import _protos as protos
        from grpc_tools.protoc import _services as services
        from grpc_tools.protoc import _protos_and_services as protos_and_services
