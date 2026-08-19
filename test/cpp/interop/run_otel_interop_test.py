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
import signal
import socket
import subprocess
import sys
import tempfile
import time


def get_free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def wait_for_port(port, host="localhost", timeout=10.0):
    start_time = time.time()
    while time.time() - start_time < timeout:
        try:
            with socket.create_connection((host, port), timeout=0.2):
                return True
        except (ConnectionRefusedError, OSError):
            time.sleep(0.05)
    return False


ROOT_DIR = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
)


def resolve_binary(path):
    if not path:
        return path
    candidates = [
        path,
        path[2:] if path.startswith("./") else path,
        os.path.abspath(path),
        os.path.join(ROOT_DIR, path),
        (
            os.path.join(ROOT_DIR, path[2:])
            if path.startswith("./")
            else os.path.join(ROOT_DIR, path)
        ),
    ]
    if path.startswith("./bazel-bin/"):
        candidates.append(path[len("./bazel-bin/") :])
        candidates.append(os.path.join(ROOT_DIR, path[len("./bazel-bin/") :]))
        candidates.append(
            os.path.join(ROOT_DIR, "bazel-bin", path[len("./bazel-bin/") :])
        )
    elif path.startswith("bazel-bin/"):
        candidates.append(path[len("bazel-bin/") :])
        candidates.append(os.path.join(ROOT_DIR, path[len("bazel-bin/") :]))
        candidates.append(
            os.path.join(ROOT_DIR, "bazel-bin", path[len("bazel-bin/") :])
        )
    for p in candidates:
        if os.path.exists(p):
            return os.path.abspath(p)
    return path


def run_cmd(args, desc, env=None, cwd=None):
    print(f"Executing: {' '.join(args)} ({desc})")
    proc_env = os.environ.copy()
    if "CC" not in proc_env and os.path.exists("/usr/bin/gcc"):
        proc_env["CC"] = "/usr/bin/gcc"
    if "CXX" not in proc_env and os.path.exists("/usr/bin/g++"):
        proc_env["CXX"] = "/usr/bin/g++"
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
    if "CC" not in proc_env and os.path.exists("/usr/bin/gcc"):
        proc_env["CC"] = "/usr/bin/gcc"
    if "CXX" not in proc_env and os.path.exists("/usr/bin/g++"):
        proc_env["CXX"] = "/usr/bin/g++"
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
        "--no_build",
        action="store_true",
        dest="skip_build",
        help="Skip building targets with Bazel/Gradle",
    )
    parser.add_argument(
        "--client",
        choices=["c++", "java", "go"],
        default="c++",
        help="Client language (c++, java, or go)",
    )
    parser.add_argument(
        "--server",
        choices=["c++", "java", "go"],
        default="c++",
        help="Server language (c++, java, or go)",
    )
    parser.add_argument(
        "--collector_bin_path",
        type=str,
        default=None,
        help="Path to OTLP collector binary",
    )
    parser.add_argument(
        "--client_bin_path",
        type=str,
        default=None,
        help="Path to client binary",
    )
    parser.add_argument(
        "--server_bin_path",
        type=str,
        default=None,
        help="Path to server binary",
    )
    args = parser.parse_args()

    is_bazel_sandbox = bool(
        os.environ.get("TEST_SRCDIR") or os.environ.get("TEST_TARGET")
    )

    if not args.skip_build and not is_bazel_sandbox:
        run_cmd(
            [
                "./tools/bazel",
                "build",
                "--macos_minimum_os=11.0",
                "//test/cpp/interop:otlp_collector",
            ],
            "Building OTLP collector",
        )
        if args.client == "c++" or args.server == "c++":
            run_cmd(
                [
                    "./tools/bazel",
                    "build",
                    "--macos_minimum_os=11.0",
                    "//test/cpp/interop:interop_client",
                    "//test/cpp/interop:interop_server",
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
                    "-PskipCodegen=true",
                ],
                "Building Java interop targets",
                cwd="../grpc-java",
            )
        if args.client == "go" or args.server == "go":
            run_cmd(
                [
                    "go",
                    "build",
                    "-o",
                    "interop/client/client",
                    "./interop/client",
                ],
                "Building Go interop client",
                cwd="../grpc-go",
            )
            run_cmd(
                [
                    "go",
                    "build",
                    "-o",
                    "interop/server/server",
                    "./interop/server",
                ],
                "Building Go interop server",
                cwd="../grpc-go",
            )

    collector_port = get_free_port()
    server_port = get_free_port()
    pid = os.getpid()
    spans_file = os.path.join(
        tempfile.gettempdir(),
        f"captured_spans_{args.client}_{args.server}_{pid}.json",
    )
    if os.path.exists(spans_file):
        os.remove(spans_file)

    collector_proc = None
    server_proc = None
    try:
        # Start Collector
        collector_bin = (
            args.collector_bin_path
            or "./bazel-bin/test/cpp/interop/otlp_collector"
        )
        collector_proc = start_proc(
            [
                resolve_binary(collector_bin),
                f"--port={collector_port}",
                f"--file={spans_file}",
            ],
            {},
            "OTLP Collector",
        )
        if not wait_for_port(collector_port, timeout=10.0):
            print(
                f"Warning: Collector port {collector_port} did not respond within timeout, proceeding anyway..."
            )

        # Base env for OTLP exporter
        base_env = {
            "OTEL_EXPORTER_OTLP_ENDPOINT": f"http://localhost:{collector_port}",
            "OTEL_EXPORTER_OTLP_PROTOCOL": "grpc",
            "OTEL_TRACES_EXPORTER": "otlp",
            "OTEL_METRICS_EXPORTER": "otlp",
            "OTEL_LOGS_EXPORTER": "none",
            "GRPC_BAZEL_RUNTIME": "1",
            "GRPC_EXPERIMENTS": "otel_export_telemetry_domains",
        }

        server_env = base_env.copy()
        if args.server in ("c++", "java"):
            server_env["GRPC_EXPERIMENTAL_ENABLE_OTEL_TRACING"] = "true"

        if args.server == "c++":
            server_bin = (
                args.server_bin_path
                or "./bazel-bin/test/cpp/interop/interop_server"
            )
            server_proc = start_proc(
                [
                    resolve_binary(server_bin),
                    f"--port={server_port}",
                    "--enable_opentelemetry=true",
                ],
                server_env,
                "C++ Interop Server",
            )
        elif args.server == "java":
            server_bin = (
                args.server_bin_path
                or "../grpc-java/interop-testing/build/install/grpc-interop-testing/bin/test-server"
            )
            server_proc = start_proc(
                [
                    resolve_binary(server_bin),
                    f"--port={server_port}",
                    "--use_tls=false",
                    "--enable_opentelemetry=true",
                ],
                server_env,
                "Java Interop Server",
            )
        elif args.server == "go":
            server_bin = (
                args.server_bin_path or "../grpc-go/interop/server/server"
            )
            server_proc = start_proc(
                [
                    resolve_binary(server_bin),
                    f"--port={server_port}",
                    "--enable_opentelemetry=true",
                ],
                server_env,
                "Go Interop Server",
            )
        # Wait for server to bind and start listening
        if not wait_for_port(server_port, timeout=10.0):
            print(
                f"Warning: Server port {server_port} did not respond within timeout, proceeding anyway..."
            )

        # Run Client
        print(f"Running {args.client.upper()} Client...")
        client_env = base_env.copy()
        if args.client in ("c++", "java"):
            client_env["GRPC_EXPERIMENTAL_ENABLE_OTEL_TRACING"] = "true"

        if args.client == "c++":
            client_bin = (
                args.client_bin_path
                or "./bazel-bin/test/cpp/interop/interop_client"
            )
            client_res = run_cmd(
                [
                    resolve_binary(client_bin),
                    "--server_host=localhost",
                    f"--server_port={server_port}",
                    "--test_case=empty_unary",
                    "--enable_opentelemetry=true",
                ],
                "Running C++ Interop Client",
                env=client_env,
            )
        elif args.client == "java":
            client_bin = (
                args.client_bin_path
                or "../grpc-java/interop-testing/build/install/grpc-interop-testing/bin/test-client"
            )
            client_res = run_cmd(
                [
                    resolve_binary(client_bin),
                    "--server_host=localhost",
                    f"--server_port={server_port}",
                    "--test_case=empty_unary",
                    "--use_tls=false",
                    "--enable_opentelemetry=true",
                ],
                "Running Java Interop Client",
                env=client_env,
            )
        elif args.client == "go":
            client_bin = (
                args.client_bin_path or "../grpc-go/interop/client/client"
            )
            client_res = run_cmd(
                [
                    resolve_binary(client_bin),
                    "--server_host=localhost",
                    f"--server_port={server_port}",
                    "--test_case=empty_unary",
                    "--use_tls=false",
                    "--enable_opentelemetry=true",
                ],
                "Running Go Interop Client",
                env=client_env,
            )

        print("Client finished.")

    finally:
        # Cleanup server and collector processes safely
        if server_proc:
            print("Terminating server...")
            try:
                server_proc.send_signal(signal.SIGINT)
                server_proc.wait(timeout=4)
            except Exception:
                try:
                    server_proc.terminate()
                    server_proc.wait(timeout=2)
                except Exception:
                    try:
                        server_proc.kill()
                    except Exception:
                        pass
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
    spans_ok = verify_spans(spans_file)
    if spans_ok:
        print("Test Result: PASSED")
        sys.exit(0)
    else:
        print("Test Result: FAILED")
        sys.exit(1)


if __name__ == "__main__":
    main()
