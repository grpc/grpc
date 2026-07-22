#!/usr/bin/env python3
#
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
#

import argparse
import json
import os
import socket
import subprocess
import sys
import time


def get_free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def run_cmd(args, desc, env=None, cwd=None):
    print(f"Executing: {' '.join(args)} ({desc})")
    proc_env = os.environ.copy()
    if env:
        proc_env.update(env)
    res = subprocess.run(
        args, env=proc_env, cwd=cwd, capture_output=True, text=True
    )
    if res.returncode != 0:
        print(f"Error executing {desc}:")
        print("STDOUT:", res.stdout)
        print("STDERR:", res.stderr)
        sys.exit(res.returncode)
    return res


def start_proc(args, env, desc, cwd=None):
    print(f"Starting in background: {' '.join(args)} ({desc})")
    # Inherit system environment and merge with custom variables
    proc_env = os.environ.copy()
    proc_env.update(env)
    return subprocess.Popen(
        args,
        env=proc_env,
        cwd=cwd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def verify_spans(spans_file):
    start_time = time.time()
    all_spans = []
    client_span = None
    attempt_span = None
    server_span = None

    print("Verifying spans with polling...")
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

    # 1. Assert all spans share the same Trace ID
    trace_id = client_span.get("trace_id")
    if attempt_span.get("trace_id") != trace_id:
        print(
            f"Assertion Failed: Attempt span Trace ID mismatch. Expected {trace_id}, got {attempt_span.get('trace_id')}"
        )
        return False
    if server_span.get("trace_id") != trace_id:
        print(
            f"Assertion Failed: Server span Trace ID mismatch. Expected {trace_id}, got {server_span.get('trace_id')}"
        )
        return False

    # 2. Assert Parent-Child hierarchy
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

    # 3. Verify Attempt Span Attributes
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

    # 4. Verify Events
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


def main():
    ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
    os.chdir(ROOT)

    parser = argparse.ArgumentParser(
        description="Build and run OpenTelemetry Interop tests locally"
    )
    parser.add_argument(
        "--skip_build",
        action="store_true",
        help="Skip building targets with Bazel/Gradle",
    )
    parser.add_argument(
        "--client",
        choices=["c++", "java", "python"],
        default="c++",
        help="Client language (c++, java, or python)",
    )
    parser.add_argument(
        "--server",
        choices=["c++", "java", "python"],
        default="c++",
        help="Server language (c++, java, or python)",
    )
    args = parser.parse_args()

    if not args.skip_build:
        if args.client == "c++" or args.server == "c++":
            run_cmd(
                [
                    "./tools/bazel",
                    "build",
                    "--macos_minimum_os=11.0",
                    "//test/cpp/interop:interop_client",
                    "//test/cpp/interop:interop_server",
                    "//test/cpp/interop:otlp_collector",
                ],
                "Building C++ interop targets",
            )
        if args.client == "java" or args.server == "java":
            run_cmd(
                [
                    "./gradlew",
                    ":grpc-interop-testing:installDist",
                    "-x",
                    "test",
                ],
                "Building Java interop targets",
                cwd="../grpc-java",
            )
        if args.client == "python" or args.server == "python":
            run_cmd(
                [
                    "./tools/bazel",
                    "build",
                    "--macos_minimum_os=11.0",
                    "//src/python/grpcio_tests/tests/interop:client",
                    "//src/python/grpcio_tests/tests/interop:server_bin",
                ],
                "Building Python interop targets",
            )

    collector_port = get_free_port()
    server_port = get_free_port()
    spans_file = os.path.abspath("captured_spans.json")
    if os.path.exists(spans_file):
        os.remove(spans_file)

    collector_proc = None
    server_proc = None
    try:
        # Start Collector
        collector_proc = start_proc(
            [
                "./bazel-bin/test/cpp/interop/otlp_collector",
                f"--port={collector_port}",
                f"--file={spans_file}",
            ],
            {},
            "OTLP Collector",
        )
        time.sleep(1)  # wait for collector to start listening

        # Start Server
        env = {
            "GRPC_EXPERIMENTAL_ENABLE_OTEL_TRACING": "true",
            "OTEL_EXPORTER_OTLP_ENDPOINT": f"http://localhost:{collector_port}",
            "OTEL_TRACES_EXPORTER": "otlp",
            "OTEL_METRICS_EXPORTER": "none",
            "OTEL_LOGS_EXPORTER": "none",
            "GRPC_BAZEL_RUNTIME": "1",
        }
        if args.server == "c++":
            server_proc = start_proc(
                [
                    "./bazel-bin/test/cpp/interop/interop_server",
                    f"--port={server_port}",
                    "--enable_opentelemetry=true",
                ],
                env,
                "C++ Interop Server",
            )
        elif args.server == "java":
            server_proc = start_proc(
                [
                    "../grpc-java/interop-testing/build/install/grpc-interop-testing/bin/test-server",
                    f"--port={server_port}",
                    "--use_tls=false",
                    "--enable_opentelemetry=true",
                ],
                env,
                "Java Interop Server",
            )
        elif args.server == "python":
            server_proc = start_proc(
                [
                    "./bazel-bin/src/python/grpcio_tests/tests/interop/server_bin",
                    f"--port={server_port}",
                    "--use_tls=false",
                    "--enable_opentelemetry=true",
                ],
                env,
                "Python Interop Server",
            )
        time.sleep(2)  # wait for server to bind and start

        # Run Client
        print(f"Running {args.client.upper()} Client...")
        if args.client == "c++":
            client_res = run_cmd(
                [
                    "./bazel-bin/test/cpp/interop/interop_client",
                    "--server_host=localhost",
                    f"--server_port={server_port}",
                    "--test_case=empty_unary",
                    "--enable_opentelemetry=true",
                ],
                "Running C++ Interop Client",
                env=env,
            )
        elif args.client == "java":
            client_res = run_cmd(
                [
                    "../grpc-java/interop-testing/build/install/grpc-interop-testing/bin/test-client",
                    "--server_host=localhost",
                    f"--server_port={server_port}",
                    "--test_case=empty_unary",
                    "--use_tls=false",
                    "--enable_opentelemetry=true",
                ],
                "Running Java Interop Client",
                env=env,
            )
        elif args.client == "python":
            client_res = run_cmd(
                [
                    "./bazel-bin/src/python/grpcio_tests/tests/interop/client",
                    "--server_host=localhost",
                    f"--server_port={server_port}",
                    "--test_case=empty_unary",
                    "--use_tls=false",
                    "--enable_opentelemetry=true",
                ],
                "Running Python Interop Client",
                env=env,
            )

        print("Client finished. Waiting for spans to flush...")
        time.sleep(2)

    finally:
        # Cleanup server and collector processes
        # Cleanup server and collector processes safely
        if server_proc:
            print("Terminating server...")
            try:
                server_proc.terminate()
                server_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    server_proc.kill()
                except Exception:
                    pass
            except Exception as e:
                print(f"Error terminating server: {e}")
        if collector_proc:
            print("Terminating collector...")
            try:
                collector_proc.terminate()
                collector_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    collector_proc.kill()
                except Exception:
                    pass
            except Exception as e:
                print(f"Error terminating collector: {e}")

    # Perform Span Verifications
    success = verify_spans(spans_file)
    if success:
        print("Test Result: PASSED")
        sys.exit(0)
    else:
        print("Test Result: FAILED")
        sys.exit(1)


if __name__ == "__main__":
    main()
