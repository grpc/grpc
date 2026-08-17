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

import os
import sys


def get_bool_env(env_var_name: str, default: bool = False) -> bool:
    val = os.getenv(env_var_name, "").lower()
    if val in {"1", "t", "true", "y", "yes"}:
        return True
    if val in {"0", "f", "false", "n", "no"}:
        return False
    return default


def kokoro_build_initiator() -> str:
    return os.getenv("KOKORO_BUILD_INITIATOR", "")


def kokoro_build_started_by_ci() -> bool:
    # Kokoro has additional accounts, f.e. kokoro-dedicated.
    return kokoro_build_initiator().startswith("kokoro")


def should_upload_results_on_ci() -> bool:
    # Only upload results when initiated by the CI.
    #
    # Right now we differentiate by checking the KOKORO_BUILD_INITIATOR env var,
    # but it may be better to identify the GitHub repo owner and skip the upload
    # not "grpc" org, but a fork. We could use KOKORO_GITHUB_COMMIT_URL,
    # but how reliable is it for all types of jobs? Alternatively, we could
    # extract it from `git remote get-url`, but this needs to be tested too.
    #
    # Note: this prints to stderr to preserve the current behavior
    # of bazel_report_helper.py. Should probably switch to a normal logger.

    # TODO(sergiitk): should we move UPLOAD_TEST_RESULTS parsing here too?
    # But we need to be sure it's propagated correctly.
    if kokoro_build_started_by_ci():
        return True

    build_initiator = kokoro_build_initiator()

    # Allow to override this behavior.
    if get_bool_env("UPLOAD_TEST_RESULTS_FORCE"):
        print(
            "[TEST UPLOAD] Force-enabling test results upload"
            f" for non-CI user {build_initiator!r}",
            file=sys.stderr,
        )
        return True

    print(
        "[TEST UPLOAD] Disabling test results upload for non-CI user"
        f" {build_initiator!r}. Enable with UPLOAD_TEST_RESULTS_FORCE=true",
        file=sys.stderr,
    )
    return False
