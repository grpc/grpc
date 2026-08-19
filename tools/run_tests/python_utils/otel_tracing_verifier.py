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

ROOT_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "../../..")
)


def resolve_binary(path):
    if not path:
        return path
    clean_path = path
    if clean_path.startswith("./"):
        clean_path = clean_path[2:]
    if clean_path.startswith("bazel-bin/"):
        clean_path = clean_path[len("bazel-bin/") :]

    candidates = [
        path,
        clean_path,
        os.path.abspath(path),
        os.path.abspath(clean_path),
        os.path.join(ROOT_DIR, path),
        os.path.join(ROOT_DIR, clean_path),
        os.path.join(ROOT_DIR, "bazel-bin", clean_path),
    ]
    test_srcdir = os.environ.get("TEST_SRCDIR")
    if test_srcdir:
        workspace = os.environ.get("TEST_WORKSPACE", "_main")
        candidates.extend(
            [
                os.path.join(test_srcdir, workspace, clean_path),
                os.path.join(test_srcdir, clean_path),
                os.path.join(test_srcdir, "com_github_grpc_grpc", clean_path),
            ]
        )

    for p in candidates:
        if os.path.exists(p):
            return os.path.abspath(p)
    return path


def verify_tracing_spans(spans_file):
    start_time = time.time()
    all_spans = []
    client_span = None
    attempt_span = None
    server_span = None

    print("Verifying tracing spans with polling...")
    while time.time() - start_time < 5.0:
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

        client_span = next(
            (
                s
                for s in all_spans
                if s.get("name")
                in (
                    "Sent.grpc.testing.TestService.EmptyCall",
                    "Sent.grpc.testing.TestService/EmptyCall",
                )
            ),
            None,
        )
        if not client_span:
            time.sleep(0.5)
            continue

        trace_id = client_span.get("trace_id")
        if not trace_id:
            time.sleep(0.5)
            continue

        attempt_span = next(
            (
                s
                for s in all_spans
                if s.get("name")
                in (
                    "Attempt.grpc.testing.TestService.EmptyCall",
                    "Attempt.grpc.testing.TestService/EmptyCall",
                )
                and s.get("trace_id") == trace_id
            ),
            None,
        )
        server_span = next(
            (
                s
                for s in all_spans
                if s.get("name")
                in (
                    "Recv.grpc.testing.TestService.EmptyCall",
                    "Recv.grpc.testing.TestService/EmptyCall",
                )
                and s.get("trace_id") == trace_id
            ),
            None,
        )

        if not attempt_span or not server_span:
            time.sleep(0.5)
            continue

        # Found all three spans matching the Trace ID
        break
    else:
        print("Assertion Failed: Timed out waiting for all 3 expected spans.")
        return False

    print(f"Collected {len(all_spans)} spans:")
    for s in all_spans:
        print(
            f"  Span: '{s.get('name')}' (TraceID: {s.get('trace_id')}, SpanID: {s.get('span_id')}, ParentID: {s.get('parent_span_id')})"
        )

    # Assert parent-child hierarchy
    # Attempt Span must be a child of Client Span
    if attempt_span.get("parent_span_id") != client_span.get("span_id"):
        print(
            f"Assertion Failed: Attempt span is not child of Client span. Expected parent {client_span.get('span_id')}, got {attempt_span.get('parent_span_id')}"
        )
        return False
    # Server Span must be a child of Attempt Span
    if server_span.get("parent_span_id") != attempt_span.get("span_id"):
        print(
            f"Assertion Failed: Server span is not child of Attempt span. Expected parent {attempt_span.get('span_id')}, got {server_span.get('parent_span_id')}"
        )
        return False

    # Verify attempt span attributes
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
            "Assertion Failed: Attribute 'previous-rpc-attempts' not found on Attempt span."
        )
        return False
    if int(prev_attempts) != 0:
        print(
            f"Assertion Failed: Attribute 'previous-rpc-attempts' value mismatch. Expected 0, got {prev_attempts}"
        )
        return False
    if trans_retry is None:
        print(
            "Assertion Failed: Attribute 'transparent-retry' not found on Attempt span."
        )
        return False
    if trans_retry is not False:
        print(
            f"Assertion Failed: Attribute 'transparent-retry' value mismatch. Expected False, got {trans_retry}"
        )
        return False

    # Verify message events
    attempt_events = [e.get("name") for e in attempt_span.get("events", [])]
    client_events = [e.get("name") for e in client_span.get("events", [])]
    if (
        "Outbound message" not in attempt_events
        and "Outbound message" not in client_events
    ):
        print(
            "Assertion Failed: Event 'Outbound message' not found on Client or Attempt span."
        )
        return False
    if (
        "Inbound message" not in attempt_events
        and "Inbound message" not in client_events
    ):
        print(
            "Assertion Failed: Event 'Inbound message' not found on Client or Attempt span."
        )
        return False

    server_events = [e.get("name") for e in server_span.get("events", [])]
    if "Inbound message" not in server_events:
        print(
            "Assertion Failed: Event 'Inbound message' not found on Server span."
        )
        return False
    if "Outbound message" not in server_events:
        print(
            "Assertion Failed: Event 'Outbound message' not found on Server span."
        )
        return False

    print("All span assertions passed successfully!")
    return True
