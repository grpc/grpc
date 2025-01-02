#!/usr/bin/env python3
# Copyright 2022 The gRPC Authors
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
"""Helps with running bazel with extra settings to generate structured test reports in CI."""

import argparse
import os
import platform
import sys
import uuid

_ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), "../../.."))
os.chdir(_ROOT)

# How long to sleep before querying Resultstore API and uploading to bigquery
# (to let ResultStore finish writing results from the bazel invocation that has
# just finished).
_UPLOAD_RBE_RESULTS_DELAY_SECONDS = 60


def _platform_string():
    """Detect current platform"""
    if platform.system() == "Windows":
        return "windows"
    elif platform.system()[:7] == "MSYS_NT":
        return "windows"
    elif platform.system() == "Darwin":
        return "mac"
    elif platform.system() == "Linux":
        return "linux"
    else:
        return "posix"


def _append_to_kokoro_bazel_invocations(invocation_id: str) -> None:
    """Kokoro can display "Bazel" result link on kokoro jobs if told so."""
    # to get "bazel" link for kokoro build, we need to upload
    # the "bazel_invocation_ids" file with bazel invocation ID as artifact.
    kokoro_artifacts_dir = os.getenv("KOKORO_ARTIFACTS_DIR")
    if kokoro_artifacts_dir:
        # append the bazel invocation UUID to the bazel_invocation_ids file.
        with open(
            os.path.join(kokoro_artifacts_dir, "bazel_invocation_ids"), "a"
        ) as f:
            f.write(invocation_id + "\n")
        print(
            'Added invocation ID %s to kokoro "bazel_invocation_ids" artifact'
            % invocation_id,
            file=sys.stderr,
        )
    else:
        print(
            'Skipped adding invocation ID %s to kokoro "bazel_invocation_ids"'
            " artifact" % invocation_id,
            file=sys.stderr,
        )
        pass


def _generate_junit_report_string(
    report_suite_name: str, invocation_id: str, success: bool
) -> None:
    """Generate sponge_log.xml formatted report, that will make the bazel invocation reachable as a target in resultstore UI / sponge."""
    bazel_invocation_url = (
        "https://source.cloud.google.com/results/invocations/%s" % invocation_id
    )
    package_name = report_suite_name
    # set testcase name to invocation URL. That way, the link will be displayed in some form
    # resultstore UI and sponge even in case the bazel invocation succeeds.
    testcase_name = bazel_invocation_url
    if success:
        # unfortunately, neither resultstore UI nor sponge display the "system-err" output (or any other tags)
        # on a passing test case. But at least we tried.
        test_output_tag = (
            "<system-err>PASSED. See invocation results here: %s</system-err>"
            % bazel_invocation_url
        )
    else:
        # The failure output will be displayes in both resultstore UI and sponge when clicking on the failing testcase.
        test_output_tag = (
            '<failure message="Failure">FAILED. See bazel invocation results'
            " here: %s</failure>" % bazel_invocation_url
        )

    lines = [
        "<testsuites>",
        '<testsuite id="1" name="%s" package="%s">'
        % (report_suite_name, package_name),
        '<testcase name="%s">' % testcase_name,
        test_output_tag,
        "</testcase></testsuite>",
        "</testsuites>",
    ]
    return "\n".join(lines)


def _create_bazel_wrapper(
    report_path: str,
    report_suite_name: str,
    invocation_id: str,
    upload_results: bool,
) -> None:
    """Create a "bazel wrapper" script that will execute bazel with extra settings and postprocessing."""

    os.makedirs(report_path, exist_ok=True)

    bazel_wrapper_filename = os.path.join(report_path, "bazel_wrapper")
    bazel_wrapper_bat_filename = bazel_wrapper_filename + ".bat"
    bazel_rc_filename = os.path.join(report_path, "bazel_wrapper.bazelrc")

    # put xml reports in a separate directory if requested by GRPC_TEST_REPORT_BASE_DIR
    report_base_dir = os.getenv("GRPC_TEST_REPORT_BASE_DIR", None)
    xml_report_path = os.path.abspath(
        os.path.join(report_base_dir, report_path)
        if report_base_dir
        else report_path
    )
    os.makedirs(xml_report_path, exist_ok=True)

    failing_report_filename = os.path.join(xml_report_path, "sponge_log.xml")
    success_report_filename = os.path.join(
        xml_report_path, "success_log_to_rename.xml"
    )

    # invoking "bash" explicitly is important for the workspace status command
    # to work on windows as well.
    workspace_status_command = (
        "bash tools/remote_build/workspace_status_kokoro.sh"
    )

    # generate RC file with the bazel flags we want to use apply.
    # Using an RC file solves problems with flag ordering in the wrapper.
    # (e.g. some flags need to come after the build/test command)
    with open(bazel_rc_filename, "w") as f:
        f.write('build --invocation_id="%s"\n' % invocation_id)
        f.write(
            'build --workspace_status_command="%s"\n' % workspace_status_command
        )

    # generate "failing" and "success" report
    # the "failing" is named as "sponge_log.xml", which is the name picked up by sponge/resultstore
    # so the failing report will be used by default (unless we later replace the report with
    # one that says "success"). That way if something goes wrong before bazel is run,
    # there will at least be a "failing" target that indicates that (we really don't want silent failures).
    with open(failing_report_filename, "w") as f:
        f.write(
            _generate_junit_report_string(
                report_suite_name, invocation_id, success=False
            )
        )
    with open(success_report_filename, "w") as f:
        f.write(
            _generate_junit_report_string(
                report_suite_name, invocation_id, success=True
            )
        )

    # generate the bazel wrapper for linux/macos
    with open(bazel_wrapper_filename, "w") as f:
        intro_lines = [
            "#!/bin/bash",
            "set -ex",
            "",
            'tools/bazel --bazelrc="%s" "$@" || FAILED=true'
            % bazel_rc_filename,
            "",
        ]

        if upload_results:
            upload_results_lines = [
                "sleep %s" % _UPLOAD_RBE_RESULTS_DELAY_SECONDS,
                "PYTHONHTTPSVERIFY=0 python3"
                " ./tools/run_tests/python_utils/upload_rbe_results.py"
                ' --invocation_id="%s"' % invocation_id,
                "",
            ]
        else:
            upload_results_lines = []

        outro_lines = [
            'if [ "$FAILED" != "" ]',
            "then",
            "  exit 1",
            "else",
            (
                "  # success: plant the pre-generated xml report that says"
                ' "success"'
            ),
            "  mv -f %s %s"
            % (success_report_filename, failing_report_filename),
            "fi",
        ]

        lines = [
            line + "\n"
            for line in intro_lines + upload_results_lines + outro_lines
        ]
        f.writelines(lines)
    os.chmod(bazel_wrapper_filename, 0o775)  # make the unix wrapper executable

    # generate bazel wrapper for windows
    with open(bazel_wrapper_bat_filename, "w") as f:
        intro_lines = [
            "@echo on",
            "",
            'bazel --bazelrc="%s" %%*' % bazel_rc_filename,
            "set BAZEL_EXITCODE=%errorlevel%",
            "",
        ]

        if upload_results:
            upload_results_lines = [
                "sleep %s" % _UPLOAD_RBE_RESULTS_DELAY_SECONDS,
                "python3 tools/run_tests/python_utils/upload_rbe_results.py"
                ' --invocation_id="%s" || exit /b 1' % invocation_id,
                "",
            ]
        else:
            upload_results_lines = []

        outro_lines = [
            "if %BAZEL_EXITCODE% == 0 (",
            (
                "  @rem success: plant the pre-generated xml report that says"
                ' "success"'
            ),
            "  mv -f %s %s"
            % (success_report_filename, failing_report_filename),
            ")",
            "exit /b %BAZEL_EXITCODE%",
        ]

        lines = [
            line + "\n"
            for line in intro_lines + upload_results_lines + outro_lines
        ]
        f.writelines(lines)

    print("Bazel invocation ID: %s" % invocation_id, file=sys.stderr)
    print(
        "Upload test results to BigQuery after bazel runs: %s" % upload_results,
        file=sys.stderr,
    )
    print(
        "Generated bazel wrapper: %s" % bazel_wrapper_filename, file=sys.stderr
    )
    print(
        "Generated bazel wrapper: %s" % bazel_wrapper_bat_filename,
        file=sys.stderr,
    )


if __name__ == "__main__":
    # parse command line
    argp = argparse.ArgumentParser(
        description=(
            "Generate bazel wrapper to help with bazel test reports in CI."
        )
    )
    argp.add_argument(
        "--report_path",
        required=True,
        type=str,
        help=(
            "Path under which the bazel wrapper and other files are going to be"
            " generated"
        ),
    )
    argp.add_argument(
        "--report_suite_name",
        default="bazel_invocations",
        type=str,
        help="Test suite name to use in generated XML report",
    )
    args = argp.parse_args()

    # generate new bazel invocation ID
    invocation_id = str(uuid.uuid4())

    report_path = args.report_path
    report_suite_name = args.report_suite_name
    upload_results = True if os.getenv("UPLOAD_TEST_RESULTS") else False

    _append_to_kokoro_bazel_invocations(invocation_id)
    _create_bazel_wrapper(
        report_path, report_suite_name, invocation_id, upload_results
    )
