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


def resolve_binary(path):
    if os.path.exists(path):
        return path
    clean_path = path.lstrip("./")
    if os.path.exists(clean_path):
        return clean_path
    if clean_path.startswith("bazel-bin/"):
        alt_path = clean_path[len("bazel-bin/") :]
        if os.path.exists(alt_path):
            return alt_path
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


def verify_metrics(metrics_file, server_lang="c++"):
    start_time = time.time()
    collected_metrics = {}

    expected_keywords = []
    if server_lang == "c++":
        expected_keywords = ["grpc.server", "grpc.tcp"]

    print(f"Verifying metrics with polling (server={server_lang})...")
    while time.time() - start_time < 5.0:
        if not os.path.exists(metrics_file):
            time.sleep(0.5)
            continue
        try:
            with open(metrics_file, "r") as f:
                requests = json.load(f)
        except (json.JSONDecodeError, IOError):
            time.sleep(0.5)
            continue

        collected_metrics = {}
        for req in requests:
            for resource_metrics in req.get("resource_metrics", []):
                for scope_metrics in resource_metrics.get("scope_metrics", []):
                    for metric in scope_metrics.get("metrics", []):
                        metric_name = metric.get("name")
                        if metric_name:
                            if metric_name not in collected_metrics:
                                collected_metrics[metric_name] = []
                            collected_metrics[metric_name].append(metric)

        if all(
            any(k in m for m in collected_metrics) for k in expected_keywords
        ):
            break
        time.sleep(0.5)

    print(
        f"Collected {len(collected_metrics)} metric names: {set(collected_metrics.keys())}"
    )
    if not collected_metrics:
        if server_lang != "c++":
            print(
                f"No metrics collected for non-C++ server ({server_lang}). TCP metrics only required for C++ servers. Metrics verification passed."
            )
            return True
        print("Assertion Failed: No metrics collected.")
        return False

    # 1. Assert domain presence
    if server_lang == "c++":
        found = all(
            any(k in m for m in collected_metrics) for k in expected_keywords
        )
        if not found:
            print(
                f"Assertion Failed: Not all of {expected_keywords} found in collected metrics."
            )
            return False

        expected_tcp_units = {
            "grpc.tcp.min_rtt": "s",
            "grpc.tcp.bytes_sent": "By",
            "grpc.tcp.connections_created": "{connection}",
            "grpc.tcp.connection_count": "{connection}",
        }
        required_tcp_metrics = {
            "grpc.tcp.bytes_sent",
            "grpc.tcp.connections_created",
            "grpc.tcp.connection_count",
        }
        for mname, expected_unit in expected_tcp_units.items():
            if mname not in collected_metrics:
                if mname in required_tcp_metrics:
                    print(
                        f"Assertion Failed: Required TCP metric '{mname}' not found."
                    )
                    return False
                continue
            metrics_with_name = collected_metrics[mname]
            actual_unit = metrics_with_name[0].get("unit", "")
            if actual_unit != expected_unit:
                print(
                    f"Assertion Failed: Metric '{mname}' unit '{actual_unit}' != expected '{expected_unit}'"
                )
                return False

        # 3. Assert TCP metric label keys
        required_label_keys = {
            "network.local.address",
            "network.local.port",
            "network.peer.address",
            "network.peer.port",
            "is_client",
        }
        found_tcp_label_keys = set()
        for mname, metrics_list in collected_metrics.items():
            if mname.startswith("grpc.tcp."):
                for metric in metrics_list:
                    for dp_type in (
                        "gauge",
                        "sum",
                        "histogram",
                        "exponential_histogram",
                    ):
                        if dp_type in metric:
                            data_points = metric[dp_type].get("data_points", [])
                            for dp in data_points:
                                for attr in dp.get("attributes", []):
                                    if "key" in attr:
                                        found_tcp_label_keys.add(attr["key"])

        missing_keys = required_label_keys - found_tcp_label_keys
        if missing_keys:
            print(
                f"Assertion Failed: Missing TCP label keys: {missing_keys}. Found keys: {found_tcp_label_keys}"
            )
            return False
    else:
        # For non-C++ servers, check if any grpc metric was recorded
        found_any = any(m.startswith("grpc.") for m in collected_metrics)
        if not found_any and len(collected_metrics) > 0:
            print("Warning: Metrics collected but none match 'grpc.*'")

    print("All metric assertions passed successfully!")
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
        choices=["c++", "java", "python", "go"],
        default="c++",
        help="Client language (c++, java, python, or go)",
    )
    parser.add_argument(
        "--server",
        choices=["c++", "java", "python", "go"],
        default="c++",
        help="Server language (c++, java, python, or go)",
    )
    args = parser.parse_args()

    if not args.skip_build:
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
    spans_file = os.path.abspath(
        f"captured_spans_{args.client}_{args.server}.json"
    )
    metrics_file = os.path.abspath(
        f"captured_metrics_{args.client}_{args.server}.json"
    )
    if os.path.exists(spans_file):
        os.remove(spans_file)
    if os.path.exists(metrics_file):
        os.remove(metrics_file)

    collector_proc = None
    server_proc = None
    try:
        # Start Collector
        collector_proc = start_proc(
            [
                resolve_binary("./bazel-bin/test/cpp/interop/otlp_collector"),
                f"--port={collector_port}",
                f"--file={spans_file}",
                f"--metrics_file={metrics_file}",
            ],
            {},
            "OTLP Collector",
        )
        time.sleep(1.5)  # wait for collector to start listening

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
            server_proc = start_proc(
                [
                    resolve_binary(
                        "./bazel-bin/test/cpp/interop/interop_server"
                    ),
                    f"--port={server_port}",
                    "--enable_opentelemetry=true",
                    "--enable_tcp_metrics=true",
                ],
                server_env,
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
                server_env,
                "Java Interop Server",
            )
        elif args.server == "python":
            server_proc = start_proc(
                [
                    resolve_binary(
                        "./bazel-bin/src/python/grpcio_tests/tests/interop/server_bin"
                    ),
                    f"--port={server_port}",
                    "--use_tls=false",
                    "--enable_opentelemetry=true",
                    "--enable_tcp_metrics=true",
                ],
                server_env,
                "Python Interop Server",
            )
        elif args.server == "go":
            server_proc = start_proc(
                [
                    "../grpc-go/interop/server/server",
                    f"--port={server_port}",
                    "--enable_opentelemetry=true",
                    "--enable_tcp_metrics=true",
                ],
                server_env,
                "Go Interop Server",
            )
        time.sleep(3.5)  # wait for server to bind and start

        # Run Client
        print(f"Running {args.client.upper()} Client...")
        client_env = base_env.copy()
        if args.client in ("c++", "java"):
            client_env["GRPC_EXPERIMENTAL_ENABLE_OTEL_TRACING"] = "true"

        if args.client == "c++":
            client_res = run_cmd(
                [
                    resolve_binary(
                        "./bazel-bin/test/cpp/interop/interop_client"
                    ),
                    "--server_host=localhost",
                    f"--server_port={server_port}",
                    "--test_case=empty_unary",
                    "--enable_opentelemetry=true",
                    "--enable_tcp_metrics=true",
                ],
                "Running C++ Interop Client",
                env=client_env,
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
                env=client_env,
            )
        elif args.client == "python":
            client_res = run_cmd(
                [
                    resolve_binary(
                        "./bazel-bin/src/python/grpcio_tests/tests/interop/client"
                    ),
                    "--server_host=localhost",
                    f"--server_port={server_port}",
                    "--test_case=empty_unary",
                    "--use_tls=false",
                    "--enable_opentelemetry=true",
                    "--enable_tcp_metrics=true",
                ],
                "Running Python Interop Client",
                env=client_env,
            )
        elif args.client == "go":
            client_res = run_cmd(
                [
                    "../grpc-go/interop/client/client",
                    "--server_host=localhost",
                    f"--server_port={server_port}",
                    "--test_case=empty_unary",
                    "--use_tls=false",
                    "--enable_opentelemetry=true",
                    "--enable_tcp_metrics=true",
                ],
                "Running Go Interop Client",
                env=client_env,
            )

        print("Client finished. Waiting for spans and metrics to flush...")
        time.sleep(2.5)

    finally:
        # Cleanup server and collector processes safely
        if server_proc:
            print("Terminating server...")
            try:
                import signal

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
        time.sleep(1.5)
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

    # Perform Span and Metric Verifications
    spans_ok = verify_spans(spans_file)
    metrics_ok = verify_metrics(metrics_file, server_lang=args.server)
    if spans_ok and metrics_ok:
        print("Test Result: PASSED")
        sys.exit(0)
    else:
        print("Test Result: FAILED")
        sys.exit(1)


if __name__ == "__main__":
    main()
