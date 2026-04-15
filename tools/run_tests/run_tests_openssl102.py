#!/usr/bin/env python3
# Copyright 2026 gRPC authors.
import sys
import os

_ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), "../.."))
os.chdir(_ROOT)

import python_utils.jobset as jobset
import python_utils.report_utils as report_utils

_DEFAULT_RUNTESTS_TIMEOUT = 6 * 60 * 60

def main():
    shortname = "run_tests_c++_linux_dbg_native_x64_gcc10.2_openssl102"
    
    cmdline = [
        "python3",
        "tools/run_tests/run_tests.py",
        "--use_docker",
        "-t",
        "-j", "2",
        "-x", "run_tests/cpp_linux_dbg_native_x64_gcc10.2_openssl102/sponge_log.xml",
        "--report_suite_name", "cpp_linux_dbg_native_x64_gcc10.2_openssl102",
        "-l", "c++",
        "-c", "dbg",
        "--iomgr_platform", "native",
        "--arch", "x64",
        "--compiler", "gcc10.2_openssl102"
    ]
    
    # Removed sys.argv forwarding to prevent unintended CLI argument bleeding

    test_job = jobset.JobSpec(
        cmdline=cmdline,
        shortname=shortname,
        timeout_seconds=_DEFAULT_RUNTESTS_TIMEOUT,
        logfilename="toplevel_run_tests_invocations/%s/sponge_log.log" % shortname,
    )

    jobset.message("START", "Running isolated OpenSSL 1.0.2 tests.", do_newline=True)
    num_failures, resultset = jobset.run([test_job], newline_on_success=True, travis=True, maxjobs=1)
    
    report_utils.render_junit_xml_report(
        resultset,
        "toplevel_run_tests_invocations/sponge_log.xml",
        suite_name="toplevel_run_tests_invocations",
        multi_target=True,
    )

    if num_failures == 0:
        jobset.message("SUCCESS", "Tests finished successfully.", do_newline=True)
        sys.exit(0)
    else:
        jobset.message("FAILED", "Tests failed.", do_newline=True)
        sys.exit(1)

if __name__ == "__main__":
    main()
