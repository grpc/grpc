#!/usr/bin/env python3
# Copyright 2016 gRPC authors.
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
"""Run performance tests locally or remotely."""

from __future__ import print_function

import argparse
import collections
import itertools
import json
import multiprocessing
import os
import pipes
import re
import subprocess
import sys
import tempfile
import time
import traceback
import uuid

import six

import performance.scenario_config as scenario_config
import python_utils.jobset as jobset
import python_utils.report_utils as report_utils

_ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), "../.."))
os.chdir(_ROOT)

_REMOTE_HOST_USERNAME = "jenkins"

_SCENARIO_TIMEOUT = 3 * 60
_WORKER_TIMEOUT = 3 * 60
_NETPERF_TIMEOUT = 60
_QUIT_WORKER_TIMEOUT = 2 * 60


class QpsWorkerJob:
    """Encapsulates a qps worker server job."""

    def __init__(self, spec, language, host_and_port, perf_file_base_name=None):
        self._spec = spec
        self.language = language
        self.host_and_port = host_and_port
        self._job = None
        self.perf_file_base_name = perf_file_base_name

    def start(self):
        self._job = jobset.Job(
            self._spec, newline_on_success=True, travis=True, add_env={}
        )

    def is_running(self):
        """Polls a job and returns True if given job is still running."""
        return self._job and self._job.state() == jobset._RUNNING

    def kill(self):
        if self._job:
            self._job.kill()
            self._job = None


def create_qpsworker_job(
    language, shortname=None, port=10000, remote_host=None, perf_cmd=None
):
    cmdline = language.worker_cmdline() + ["--driver_port=%s" % port]

    if remote_host:
        host_and_port = "%s:%s" % (remote_host, port)
    else:
        host_and_port = "localhost:%s" % port

    perf_file_base_name = None
    if perf_cmd:
        perf_file_base_name = "%s-%s" % (host_and_port, shortname)
        # specify -o output file so perf.data gets collected when worker stopped
        cmdline = (
            perf_cmd + ["-o", "%s-perf.data" % perf_file_base_name] + cmdline
        )

    worker_timeout = _WORKER_TIMEOUT
    if remote_host:
        user_at_host = "%s@%s" % (_REMOTE_HOST_USERNAME, remote_host)
        ssh_cmd = ["ssh"]
        cmdline = ["timeout", "%s" % (worker_timeout + 30)] + cmdline
        ssh_cmd.extend(
            [
                str(user_at_host),
                "cd ~/performance_workspace/grpc/ && %s" % " ".join(cmdline),
            ]
        )
        cmdline = ssh_cmd

    jobspec = jobset.JobSpec(
        cmdline=cmdline,
        shortname=shortname,
        timeout_seconds=worker_timeout,  # workers get restarted after each scenario
        verbose_success=True,
    )
    return QpsWorkerJob(jobspec, language, host_and_port, perf_file_base_name)


def create_scenario_jobspec(
    scenario_json,
    workers,
    remote_host=None,
    bq_result_table=None,
    server_cpu_load=0,
):
    """Runs one scenario using QPS driver."""
    # setting QPS_WORKERS env variable here makes sure it works with SSH too.
    cmd = 'QPS_WORKERS="%s" ' % ",".join(workers)
    if bq_result_table:
        cmd += 'BQ_RESULT_TABLE="%s" ' % bq_result_table
    cmd += "tools/run_tests/performance/run_qps_driver.sh "
    cmd += "--scenarios_json=%s " % pipes.quote(
        json.dumps({"scenarios": [scenario_json]})
    )
    cmd += "--scenario_result_file=scenario_result.json "
    if server_cpu_load != 0:
        cmd += (
            "--search_param=offered_load --initial_search_value=1000"
            " --targeted_cpu_load=%d --stride=500 --error_tolerance=0.01"
            % server_cpu_load
        )
    if remote_host:
        user_at_host = "%s@%s" % (_REMOTE_HOST_USERNAME, remote_host)
        cmd = 'ssh %s "cd ~/performance_workspace/grpc/ && "%s' % (
            user_at_host,
            pipes.quote(cmd),
        )

    return jobset.JobSpec(
        cmdline=[cmd],
        shortname="%s" % scenario_json["name"],
        timeout_seconds=_SCENARIO_TIMEOUT,
        shell=True,
        verbose_success=True,
    )


def create_quit_jobspec(workers, remote_host=None):
    """Runs quit using QPS driver."""
    # setting QPS_WORKERS env variable here makes sure it works with SSH too.
    cmd = 'QPS_WORKERS="%s" cmake/build/qps_json_driver --quit' % ",".join(
        w.host_and_port for w in workers
    )
    if remote_host:
        user_at_host = "%s@%s" % (_REMOTE_HOST_USERNAME, remote_host)
        cmd = 'ssh %s "cd ~/performance_workspace/grpc/ && "%s' % (
            user_at_host,
            pipes.quote(cmd),
        )

    return jobset.JobSpec(
        cmdline=[cmd],
        shortname="shutdown_workers",
        timeout_seconds=_QUIT_WORKER_TIMEOUT,
        shell=True,
        verbose_success=True,
    )


def create_netperf_jobspec(
    server_host="localhost", client_host=None, bq_result_table=None
):
    """Runs netperf benchmark."""
    cmd = 'NETPERF_SERVER_HOST="%s" ' % server_host
    if bq_result_table:
        cmd += 'BQ_RESULT_TABLE="%s" ' % bq_result_table
    if client_host:
        # If netperf is running remotely, the env variables populated by Jenkins
        # won't be available on the client, but we need them for uploading results
        # to BigQuery.
        jenkins_job_name = os.getenv("KOKORO_JOB_NAME")
        if jenkins_job_name:
            cmd += 'KOKORO_JOB_NAME="%s" ' % jenkins_job_name
        jenkins_build_number = os.getenv("KOKORO_BUILD_NUMBER")
        if jenkins_build_number:
            cmd += 'KOKORO_BUILD_NUMBER="%s" ' % jenkins_build_number

    cmd += "tools/run_tests/performance/run_netperf.sh"
    if client_host:
        user_at_host = "%s@%s" % (_REMOTE_HOST_USERNAME, client_host)
        cmd = 'ssh %s "cd ~/performance_workspace/grpc/ && "%s' % (
            user_at_host,
            pipes.quote(cmd),
        )

    return jobset.JobSpec(
        cmdline=[cmd],
        shortname="netperf",
        timeout_seconds=_NETPERF_TIMEOUT,
        shell=True,
        verbose_success=True,
    )


def archive_repo(languages):
    """Archives local version of repo including submodules."""
    cmdline = ["tar", "-cf", "../grpc.tar", "../grpc/"]
    if "java" in languages:
        cmdline.append("../grpc-java")
    if "go" in languages:
        cmdline.append("../grpc-go")
    if "node" in languages or "node_purejs" in languages:
        cmdline.append("../grpc-node")

    archive_job = jobset.JobSpec(
        cmdline=cmdline, shortname="archive_repo", timeout_seconds=3 * 60
    )

    jobset.message("START", "Archiving local repository.", do_newline=True)
    num_failures, _ = jobset.run(
        [archive_job], newline_on_success=True, maxjobs=1
    )
    if num_failures == 0:
        jobset.message(
            "SUCCESS",
            "Archive with local repository created successfully.",
            do_newline=True,
        )
    else:
        jobset.message(
            "FAILED", "Failed to archive local repository.", do_newline=True
        )
        sys.exit(1)


def prepare_remote_hosts(hosts, prepare_local=False):
    """Prepares remote hosts (and maybe prepare localhost as well)."""
    prepare_timeout = 10 * 60
    prepare_jobs = []
    for host in hosts:
        user_at_host = "%s@%s" % (_REMOTE_HOST_USERNAME, host)
        prepare_jobs.append(
            jobset.JobSpec(
                cmdline=["tools/run_tests/performance/remote_host_prepare.sh"],
                shortname="remote_host_prepare.%s" % host,
                environ={"USER_AT_HOST": user_at_host},
                timeout_seconds=prepare_timeout,
            )
        )
    if prepare_local:
        # Prepare localhost as well
        prepare_jobs.append(
            jobset.JobSpec(
                cmdline=["tools/run_tests/performance/kill_workers.sh"],
                shortname="local_prepare",
                timeout_seconds=prepare_timeout,
            )
        )
    jobset.message("START", "Preparing hosts.", do_newline=True)
    num_failures, _ = jobset.run(
        prepare_jobs, newline_on_success=True, maxjobs=10
    )
    if num_failures == 0:
        jobset.message(
            "SUCCESS", "Prepare step completed successfully.", do_newline=True
        )
    else:
        jobset.message(
            "FAILED", "Failed to prepare remote hosts.", do_newline=True
        )
        sys.exit(1)


def build_on_remote_hosts(
    hosts, languages=list(scenario_config.LANGUAGES.keys()), build_local=False
):
    """Builds performance worker on remote hosts (and maybe also locally)."""
    build_timeout = 45 * 60
    # Kokoro VMs (which are local only) do not have caching, so they need more time to build
    local_build_timeout = 60 * 60
    build_jobs = []
    for host in hosts:
        user_at_host = "%s@%s" % (_REMOTE_HOST_USERNAME, host)
        build_jobs.append(
            jobset.JobSpec(
                cmdline=["tools/run_tests/performance/remote_host_build.sh"]
                + languages,
                shortname="remote_host_build.%s" % host,
                environ={"USER_AT_HOST": user_at_host, "CONFIG": "opt"},
                timeout_seconds=build_timeout,
            )
        )
    if build_local:
        # start port server locally
        build_jobs.append(
            jobset.JobSpec(
                cmdline=["python", "tools/run_tests/start_port_server.py"],
                shortname="local_start_port_server",
                timeout_seconds=2 * 60,
            )
        )
        # Build locally as well
        build_jobs.append(
            jobset.JobSpec(
                cmdline=["tools/run_tests/performance/build_performance.sh"]
                + languages,
                shortname="local_build",
                environ={"CONFIG": "opt"},
                timeout_seconds=local_build_timeout,
            )
        )
    jobset.message("START", "Building.", do_newline=True)
    num_failures, _ = jobset.run(
        build_jobs, newline_on_success=True, maxjobs=10
    )
    if num_failures == 0:
        jobset.message("SUCCESS", "Built successfully.", do_newline=True)
    else:
        jobset.message("FAILED", "Build failed.", do_newline=True)
        sys.exit(1)


def create_qpsworkers(languages, worker_hosts, perf_cmd=None):
    """Creates QPS workers (but does not start them)."""
    if not worker_hosts:
        # run two workers locally (for each language)
        workers = [(None, 10000), (None, 10010)]
    elif len(worker_hosts) == 1:
        # run two workers on the remote host (for each language)
        workers = [(worker_hosts[0], 10000), (worker_hosts[0], 10010)]
    else:
        # run one worker per each remote host (for each language)
        workers = [(worker_host, 10000) for worker_host in worker_hosts]

    return [
        create_qpsworker_job(
            language,
            shortname="qps_worker_%s_%s" % (language, worker_idx),
            port=worker[1] + language.worker_port_offset(),
            remote_host=worker[0],
            perf_cmd=perf_cmd,
        )
        for language in languages
        for worker_idx, worker in enumerate(workers)
    ]


def perf_report_processor_job(
    worker_host, perf_base_name, output_filename, flame_graph_reports
):
    print("Creating perf report collection job for %s" % worker_host)
    cmd = ""
    if worker_host != "localhost":
        user_at_host = "%s@%s" % (_REMOTE_HOST_USERNAME, worker_host)
        cmd = (
            "USER_AT_HOST=%s OUTPUT_FILENAME=%s OUTPUT_DIR=%s PERF_BASE_NAME=%s"
            " tools/run_tests/performance/process_remote_perf_flamegraphs.sh"
            % (
                user_at_host,
                output_filename,
                flame_graph_reports,
                perf_base_name,
            )
        )
    else:
        cmd = (
            "OUTPUT_FILENAME=%s OUTPUT_DIR=%s PERF_BASE_NAME=%s"
            " tools/run_tests/performance/process_local_perf_flamegraphs.sh"
            % (output_filename, flame_graph_reports, perf_base_name)
        )

    return jobset.JobSpec(
        cmdline=cmd,
        timeout_seconds=3 * 60,
        shell=True,
        verbose_success=True,
        shortname="process perf report",
    )


Scenario = collections.namedtuple("Scenario", "jobspec workers name")


def create_scenarios(
    languages,
    workers_by_lang,
    remote_host=None,
    regex=".*",
    category="all",
    bq_result_table=None,
    netperf=False,
    netperf_hosts=[],
    server_cpu_load=0,
):
    """Create jobspecs for scenarios to run."""
    all_workers = [
        worker
        for workers in list(workers_by_lang.values())
        for worker in workers
    ]
    scenarios = []
    _NO_WORKERS = []

    if netperf:
        if not netperf_hosts:
            netperf_server = "localhost"
            netperf_client = None
        elif len(netperf_hosts) == 1:
            netperf_server = netperf_hosts[0]
            netperf_client = netperf_hosts[0]
        else:
            netperf_server = netperf_hosts[0]
            netperf_client = netperf_hosts[1]
        scenarios.append(
            Scenario(
                create_netperf_jobspec(
                    server_host=netperf_server,
                    client_host=netperf_client,
                    bq_result_table=bq_result_table,
                ),
                _NO_WORKERS,
                "netperf",
            )
        )

    for language in languages:
        for scenario_json in language.scenarios():
            if re.search(regex, scenario_json["name"]):
                categories = scenario_json.get(
                    "CATEGORIES", ["scalable", "smoketest"]
                )
                if category in categories or category == "all":
                    workers = workers_by_lang[str(language)][:]
                    # 'SERVER_LANGUAGE' is an indicator for this script to pick
                    # a server in different language.
                    custom_server_lang = scenario_json.get(
                        "SERVER_LANGUAGE", None
                    )
                    custom_client_lang = scenario_json.get(
                        "CLIENT_LANGUAGE", None
                    )
                    scenario_json = scenario_config.remove_nonproto_fields(
                        scenario_json
                    )
                    if custom_server_lang and custom_client_lang:
                        raise Exception(
                            "Cannot set both custom CLIENT_LANGUAGE and"
                            " SERVER_LANGUAGEin the same scenario"
                        )
                    if custom_server_lang:
                        if not workers_by_lang.get(custom_server_lang, []):
                            print(
                                "Warning: Skipping scenario %s as"
                                % scenario_json["name"]
                            )
                            print(
                                "SERVER_LANGUAGE is set to %s yet the language"
                                " has not been selected with -l"
                                % custom_server_lang
                            )
                            continue
                        for idx in range(0, scenario_json["num_servers"]):
                            # replace first X workers by workers of a different language
                            workers[idx] = workers_by_lang[custom_server_lang][
                                idx
                            ]
                    if custom_client_lang:
                        if not workers_by_lang.get(custom_client_lang, []):
                            print(
                                "Warning: Skipping scenario %s as"
                                % scenario_json["name"]
                            )
                            print(
                                "CLIENT_LANGUAGE is set to %s yet the language"
                                " has not been selected with -l"
                                % custom_client_lang
                            )
                            continue
                        for idx in range(
                            scenario_json["num_servers"], len(workers)
                        ):
                            # replace all client workers by workers of a different language,
                            # leave num_server workers as they are server workers.
                            workers[idx] = workers_by_lang[custom_client_lang][
                                idx
                            ]
                    scenario = Scenario(
                        create_scenario_jobspec(
                            scenario_json,
                            [w.host_and_port for w in workers],
                            remote_host=remote_host,
                            bq_result_table=bq_result_table,
                            server_cpu_load=server_cpu_load,
                        ),
                        workers,
                        scenario_json["name"],
                    )
                    scenarios.append(scenario)

    return scenarios


def finish_qps_workers(jobs, qpsworker_jobs):
    """Waits for given jobs to finish and eventually kills them."""
    retries = 0
    num_killed = 0
    while any(job.is_running() for job in jobs):
        for job in qpsworker_jobs:
            if job.is_running():
                print('QPS worker "%s" is still running.' % job.host_and_port)
        if retries > 10:
            print("Killing all QPS workers.")
            for job in jobs:
                job.kill()
                num_killed += 1
        retries += 1
        time.sleep(3)
    print("All QPS workers finished.")
    return num_killed


profile_output_files = []


# Collect perf text reports and flamegraphs if perf_cmd was used
# Note the base names of perf text reports are used when creating and processing
# perf data. The scenario name uniqifies the output name in the final
# perf reports directory.
# Alos, the perf profiles need to be fetched and processed after each scenario
# in order to avoid clobbering the output files.
def run_collect_perf_profile_jobs(
    hosts_and_base_names, scenario_name, flame_graph_reports
):
    perf_report_jobs = []
    global profile_output_files
    for host_and_port in hosts_and_base_names:
        perf_base_name = hosts_and_base_names[host_and_port]
        output_filename = "%s-%s" % (scenario_name, perf_base_name)
        # from the base filename, create .svg output filename
        host = host_and_port.split(":")[0]
        profile_output_files.append("%s.svg" % output_filename)
        perf_report_jobs.append(
            perf_report_processor_job(
                host, perf_base_name, output_filename, flame_graph_reports
            )
        )

    jobset.message(
        "START", "Collecting perf reports from qps workers", do_newline=True
    )
    failures, _ = jobset.run(
        perf_report_jobs, newline_on_success=True, maxjobs=1
    )
    jobset.message(
        "SUCCESS", "Collecting perf reports from qps workers", do_newline=True
    )
    return failures


def main():
    argp = argparse.ArgumentParser(description="Run performance tests.")
    argp.add_argument(
        "-l",
        "--language",
        choices=["all"] + sorted(scenario_config.LANGUAGES.keys()),
        nargs="+",
        required=True,
        help="Languages to benchmark.",
    )
    argp.add_argument(
        "--remote_driver_host",
        default=None,
        help=(
            "Run QPS driver on given host. By default, QPS driver is run"
            " locally."
        ),
    )
    argp.add_argument(
        "--remote_worker_host",
        nargs="+",
        default=[],
        help="Worker hosts where to start QPS workers.",
    )
    argp.add_argument(
        "--dry_run",
        default=False,
        action="store_const",
        const=True,
        help="Just list scenarios to be run, but don't run them.",
    )
    argp.add_argument(
        "-r",
        "--regex",
        default=".*",
        type=str,
        help="Regex to select scenarios to run.",
    )
    argp.add_argument(
        "--bq_result_table",
        default=None,
        type=str,
        help='Bigquery "dataset.table" to upload results to.',
    )
    argp.add_argument(
        "--category",
        choices=["smoketest", "all", "scalable", "sweep"],
        default="all",
        help="Select a category of tests to run.",
    )
    argp.add_argument(
        "--netperf",
        default=False,
        action="store_const",
        const=True,
        help="Run netperf benchmark as one of the scenarios.",
    )
    argp.add_argument(
        "--server_cpu_load",
        default=0,
        type=int,
        help=(
            "Select a targeted server cpu load to run. 0 means ignore this flag"
        ),
    )
    argp.add_argument(
        "-x",
        "--xml_report",
        default="report.xml",
        type=str,
        help="Name of XML report file to generate.",
    )
    argp.add_argument(
        "--perf_args",
        help=(
            'Example usage: "--perf_args=record -F 99 -g". '
            "Wrap QPS workers in a perf command "
            "with the arguments to perf specified here. "
            '".svg" flame graph profiles will be '
            "created for each Qps Worker on each scenario. "
            'Files will output to "<repo_root>/<args.flame_graph_reports>" '
            "directory. Output files from running the worker "
            "under perf are saved in the repo root where its ran. "
            'Note that the perf "-g" flag is necessary for '
            "flame graphs generation to work (assuming the binary "
            "being profiled uses frame pointers, check out "
            '"--call-graph dwarf" option using libunwind otherwise.) '
            'Also note that the entire "--perf_args=<arg(s)>" must '
            "be wrapped in quotes as in the example usage. "
            'If the "--perg_args" is unspecified, "perf" will '
            "not be used at all. "
            "See http://www.brendangregg.com/perf.html "
            "for more general perf examples."
        ),
    )
    argp.add_argument(
        "--skip_generate_flamegraphs",
        default=False,
        action="store_const",
        const=True,
        help=(
            "Turn flame graph generation off. "
            'May be useful if "perf_args" arguments do not make sense for '
            'generating flamegraphs (e.g., "--perf_args=stat ...")'
        ),
    )
    argp.add_argument(
        "-f",
        "--flame_graph_reports",
        default="perf_reports",
        type=str,
        help=(
            "Name of directory to output flame graph profiles to, if any are"
            " created."
        ),
    )
    argp.add_argument(
        "-u",
        "--remote_host_username",
        default="",
        type=str,
        help='Use a username that isn\'t "Jenkins" to SSH into remote workers.',
    )

    args = argp.parse_args()

    global _REMOTE_HOST_USERNAME
    if args.remote_host_username:
        _REMOTE_HOST_USERNAME = args.remote_host_username

    languages = set(
        scenario_config.LANGUAGES[l]
        for l in itertools.chain.from_iterable(
            six.iterkeys(scenario_config.LANGUAGES) if x == "all" else [x]
            for x in args.language
        )
    )

    # Put together set of remote hosts where to run and build
    remote_hosts = set()
    if args.remote_worker_host:
        for host in args.remote_worker_host:
            remote_hosts.add(host)
    if args.remote_driver_host:
        remote_hosts.add(args.remote_driver_host)

    if not args.dry_run:
        if remote_hosts:
            archive_repo(languages=[str(l) for l in languages])
            prepare_remote_hosts(remote_hosts, prepare_local=True)
        else:
            prepare_remote_hosts([], prepare_local=True)

    build_local = False
    if not args.remote_driver_host:
        build_local = True
    if not args.dry_run:
        build_on_remote_hosts(
            remote_hosts,
            languages=[str(l) for l in languages],
            build_local=build_local,
        )

    perf_cmd = None
    if args.perf_args:
        print("Running workers under perf profiler")
        # Expect /usr/bin/perf to be installed here, as is usual
        perf_cmd = ["/usr/bin/perf"]
        perf_cmd.extend(re.split("\s+", args.perf_args))

    qpsworker_jobs = create_qpsworkers(
        languages, args.remote_worker_host, perf_cmd=perf_cmd
    )

    # get list of worker addresses for each language.
    workers_by_lang = dict([(str(language), []) for language in languages])
    for job in qpsworker_jobs:
        workers_by_lang[str(job.language)].append(job)

    scenarios = create_scenarios(
        languages,
        workers_by_lang=workers_by_lang,
        remote_host=args.remote_driver_host,
        regex=args.regex,
        category=args.category,
        bq_result_table=args.bq_result_table,
        netperf=args.netperf,
        netperf_hosts=args.remote_worker_host,
        server_cpu_load=args.server_cpu_load,
    )

    if not scenarios:
        raise Exception("No scenarios to run")

    total_scenario_failures = 0
    qps_workers_killed = 0
    merged_resultset = {}
    perf_report_failures = 0

    for scenario in scenarios:
        if args.dry_run:
            print(scenario.name)
        else:
            scenario_failures = 0
            try:
                for worker in scenario.workers:
                    worker.start()
                jobs = [scenario.jobspec]
                if scenario.workers:
                    # TODO(jtattermusch): ideally the "quit" job won't show up
                    # in the report
                    jobs.append(
                        create_quit_jobspec(
                            scenario.workers,
                            remote_host=args.remote_driver_host,
                        )
                    )
                scenario_failures, resultset = jobset.run(
                    jobs, newline_on_success=True, maxjobs=1
                )
                total_scenario_failures += scenario_failures
                merged_resultset = dict(
                    itertools.chain(
                        six.iteritems(merged_resultset),
                        six.iteritems(resultset),
                    )
                )
            finally:
                # Consider qps workers that need to be killed as failures
                qps_workers_killed += finish_qps_workers(
                    scenario.workers, qpsworker_jobs
                )

            if (
                perf_cmd
                and scenario_failures == 0
                and not args.skip_generate_flamegraphs
            ):
                workers_and_base_names = {}
                for worker in scenario.workers:
                    if not worker.perf_file_base_name:
                        raise Exception(
                            "using perf buf perf report filename is unspecified"
                        )
                    workers_and_base_names[
                        worker.host_and_port
                    ] = worker.perf_file_base_name
                perf_report_failures += run_collect_perf_profile_jobs(
                    workers_and_base_names,
                    scenario.name,
                    args.flame_graph_reports,
                )

    # Still write the index.html even if some scenarios failed.
    # 'profile_output_files' will only have names for scenarios that passed
    if perf_cmd and not args.skip_generate_flamegraphs:
        # write the index fil to the output dir, with all profiles from all scenarios/workers
        report_utils.render_perf_profiling_results(
            "%s/index.html" % args.flame_graph_reports, profile_output_files
        )

    report_utils.render_junit_xml_report(
        merged_resultset,
        args.xml_report,
        suite_name="benchmarks",
        multi_target=True,
    )

    if total_scenario_failures > 0 or qps_workers_killed > 0:
        print(
            "%s scenarios failed and %s qps worker jobs killed"
            % (total_scenario_failures, qps_workers_killed)
        )
        sys.exit(1)

    if perf_report_failures > 0:
        print("%s perf profile collection jobs failed" % perf_report_failures)
        sys.exit(1)


if __name__ == "__main__":
    main()
