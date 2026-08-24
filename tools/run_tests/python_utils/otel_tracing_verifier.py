# Copyright 2026 gRPC authors.
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
"""OpenTelemetry Tracing Span Verification Utilities."""

import json
import os
import time


def verify_tracing_spans(spans_file, expected_runs_count=1, poll_timeout=30.0):
    start_time = time.time()
    all_spans = []
    spans_by_trace = {}
    client_spans = []

    print(
        f"Verifying tracing spans for {expected_runs_count} expected run(s) with polling..."
    )
    while time.time() - start_time < poll_timeout:
        if not os.path.exists(spans_file):
            time.sleep(0.5)
            continue
        try:
            with open(spans_file, "r") as f:
                requests = json.load(f)
        except (json.JSONDecodeError, IOError):
            time.sleep(0.5)
            continue

        all_spans = []
        for req in requests:
            for resource_spans in req.get("resource_spans", []):
                for scope_spans in resource_spans.get("scope_spans", []):
                    for span in scope_spans.get("spans", []):
                        all_spans.append(span)

        # Group spans by trace_id globally
        spans_by_trace = {}
        for s in all_spans:
            tid = s.get("trace_id")
            if tid:
                spans_by_trace.setdefault(tid, []).append(s)

        # Identify all client spans
        client_spans = [
            s
            for s in all_spans
            if s.get("name")
            in (
                "Sent.grpc.testing.TestService.EmptyCall",
                "Sent.grpc.testing.TestService/EmptyCall",
            )
        ]

        if len(client_spans) < expected_runs_count:
            time.sleep(0.5)
            continue

        # Check if attempt and server spans are present for all client traces
        all_traces_complete = True
        for client_span in client_spans:
            trace_id = client_span.get("trace_id")
            trace_spans = spans_by_trace.get(trace_id, [])

            attempt_span = next(
                (
                    s
                    for s in trace_spans
                    if s.get("name")
                    in (
                        "Attempt.grpc.testing.TestService.EmptyCall",
                        "Attempt.grpc.testing.TestService/EmptyCall",
                    )
                ),
                None,
            )
            server_span = next(
                (
                    s
                    for s in trace_spans
                    if s.get("name")
                    in (
                        "Recv.grpc.testing.TestService.EmptyCall",
                        "Recv.grpc.testing.TestService/EmptyCall",
                    )
                ),
                None,
            )

            if not attempt_span or not server_span:
                all_traces_complete = False
                break

        if not all_traces_complete:
            time.sleep(0.5)
            continue

        # All expected traces have client, attempt, and server spans
        break
    else:
        print(
            f"Assertion Failed: Timed out waiting for {expected_runs_count} complete trace(s)."
        )
        print(f"Collected {len(all_spans)} total span(s):")
        for s in all_spans:
            print(
                f"  Span: '{s.get('name')}' (TraceID: {s.get('trace_id')}, SpanID: {s.get('span_id')}, ParentID: {s.get('parent_span_id')})"
            )
        print(f"Grouped into {len(spans_by_trace)} trace(s):")
        for tid, s_list in spans_by_trace.items():
            names = [s.get("name") for s in s_list]
            print(f"  Trace {tid}: {names}")
        return False

    print(
        f"Collected {len(all_spans)} total spans across {len(client_spans)} client run(s):"
    )
    for s in all_spans:
        print(
            f"  Span: '{s.get('name')}' (TraceID: {s.get('trace_id')}, SpanID: {s.get('span_id')}, ParentID: {s.get('parent_span_id')})"
        )

    # Thoroughly validate each trace group
    for idx, client_span in enumerate(client_spans, 1):
        trace_id = client_span.get("trace_id")
        trace_spans = spans_by_trace.get(trace_id, [])

        attempt_span = next(
            s
            for s in trace_spans
            if s.get("name")
            in (
                "Attempt.grpc.testing.TestService.EmptyCall",
                "Attempt.grpc.testing.TestService/EmptyCall",
            )
        )
        server_span = next(
            s
            for s in trace_spans
            if s.get("name")
            in (
                "Recv.grpc.testing.TestService.EmptyCall",
                "Recv.grpc.testing.TestService/EmptyCall",
            )
        )

        print(f"Verifying Trace {idx}/{len(client_spans)} (TraceID: {trace_id}):")

        # 1. Assert parent-child hierarchy
        if attempt_span.get("parent_span_id") != client_span.get("span_id"):
            print(
                f"Assertion Failed in Trace {trace_id}: Attempt span is not child of Client span. "
                f"Expected parent {client_span.get('span_id')}, got {attempt_span.get('parent_span_id')}"
            )
            return False
        if server_span.get("parent_span_id") != attempt_span.get("span_id"):
            print(
                f"Assertion Failed in Trace {trace_id}: Server span is not child of Attempt span. "
                f"Expected parent {attempt_span.get('span_id')}, got {server_span.get('parent_span_id')}"
            )
            return False

        # 2. Verify attempt span attributes
        attributes = attempt_span.get("attributes", [])
        prev_attempts = None
        trans_retry = None
        for attr in attributes:
            key = attr.get("key")
            val_dict = attr.get("value", {})
            if key == "previous-rpc-attempts":
                prev_attempts = val_dict.get("int_value")
            elif key == "transparent-retry":
                trans_retry = val_dict.get("bool_value")

        if prev_attempts is None:
            print(
                f"Assertion Failed in Trace {trace_id}: Attribute 'previous-rpc-attempts' not found on Attempt span."
            )
            return False
        if int(prev_attempts) != 0:
            print(
                f"Assertion Failed in Trace {trace_id}: Attribute 'previous-rpc-attempts' value mismatch. Expected 0, got {prev_attempts}"
            )
            return False
        if trans_retry is None:
            print(
                f"Assertion Failed in Trace {trace_id}: Attribute 'transparent-retry' not found on Attempt span."
            )
            return False
        if trans_retry is not False:
            print(
                f"Assertion Failed in Trace {trace_id}: Attribute 'transparent-retry' value mismatch. Expected False, got {trans_retry}"
            )
            return False

        # 3. Verify message events
        attempt_events = [e.get("name") for e in attempt_span.get("events", [])]
        client_events = [e.get("name") for e in client_span.get("events", [])]
        if (
            "Outbound message" not in attempt_events
            and "Outbound message" not in client_events
        ):
            print(
                f"Assertion Failed in Trace {trace_id}: Event 'Outbound message' not found on Client or Attempt span."
            )
            return False
        if (
            "Inbound message" not in attempt_events
            and "Inbound message" not in client_events
        ):
            print(
                f"Assertion Failed in Trace {trace_id}: Event 'Inbound message' not found on Client or Attempt span."
            )
            return False

        server_events = [e.get("name") for e in server_span.get("events", [])]
        if "Inbound message" not in server_events:
            print(
                f"Assertion Failed in Trace {trace_id}: Event 'Inbound message' not found on Server span."
            )
            return False
        if "Outbound message" not in server_events:
            print(
                f"Assertion Failed in Trace {trace_id}: Event 'Outbound message' not found on Server span."
            )
            return False

    print(f"All {len(client_spans)} trace(s) verified successfully!")
    return True
