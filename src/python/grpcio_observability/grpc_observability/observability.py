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

import sys
import grpc

from opencensus.trace import execution_context
from opencensus.trace import span_context as span_context_module
from opencensus.trace import trace_options as trace_options_module

from grpc_observability import _cyobservability

class Observability:

    def __init__(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        _cyobservability.at_observability_exit()

    def init(self) -> None:
        # 1. Read config.
        # 2. Creating measures and register views.
        # 3. Create and Saves Tracer and Sampler to ContextVar.
        # TODO(xuanwn): Errors out if config is invalid.
        _cyobservability.read_gcp_observability_config()

        # 4. Start exporting thread.
        _cyobservability.observability_init()

        # 5. Inject server call tracer factory.
        server_call_tracer_factory = _cyobservability.create_server_call_tracer_factory_capsule()
        grpc.observability_init(server_call_tracer_factory)


def _create_client_call_tracer_capsule(**kwargs) -> object:
    method = kwargs['method']
    # Propagate existing OC context
    current_span = execution_context.get_current_span()
    if current_span:
        trace_id = current_span.context_tracer.trace_id.encode('utf8')
        parent_span_id = current_span.span_id.encode('utf8')
        capsule = _cyobservability.create_client_call_tracer_capsule(
            method, trace_id, parent_span_id)
    else:
        trace_id = span_context_module.generate_trace_id().encode('utf8')
        capsule = _cyobservability.create_client_call_tracer_capsule(
            method, trace_id)
    return capsule


def _save_span_context(**kwargs) -> None:
    """Used to propagate context from gRPC server to OC.
    """
    trace_options = trace_options_module.TraceOptions(0)
    trace_options.set_enabled(kwargs['is_sampled'])
    span_context = span_context_module.SpanContext(trace_id=kwargs['trace_id'],
                                                   span_id=kwargs['span_id'],
                                                   trace_options=trace_options)
    current_tracer = execution_context.get_opencensus_tracer()
    current_tracer.span_context = span_context
