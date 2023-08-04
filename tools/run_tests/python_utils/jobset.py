# Copyright 2015 gRPC authors.
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
"""Run a group of subprocesses and then finish."""

import errno
import logging
import multiprocessing
import os
import platform
import re
import signal
import subprocess
import sys
import tempfile
import time

# cpu cost measurement
measure_cpu_costs = False

_DEFAULT_MAX_JOBS = 16 * multiprocessing.cpu_count()
# Maximum number of bytes of job's stdout that will be stored in the result.
# Only last N bytes of stdout will be kept if the actual output longer.
_MAX_RESULT_SIZE = 64 * 1024


# NOTE: If you change this, please make sure to test reviewing the
# github PR with http://reviewable.io, which is known to add UTF-8
# characters to the PR description, which leak into the environment here
# and cause failures.
def strip_non_ascii_chars(s):
    return "".join(c for c in s if ord(c) < 128)


def sanitized_environment(env):
    sanitized = {}
    for key, value in list(env.items()):
        sanitized[strip_non_ascii_chars(key)] = strip_non_ascii_chars(value)
    return sanitized


def platform_string():
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


# setup a signal handler so that signal.pause registers 'something'
# when a child finishes
# not using futures and threading to avoid a dependency on subprocess32
if platform_string() == "windows":
    pass
else:

    def alarm_handler(unused_signum, unused_frame):
        pass

    signal.signal(signal.SIGCHLD, lambda unused_signum, unused_frame: None)
    signal.signal(signal.SIGALRM, alarm_handler)

_SUCCESS = object()
_FAILURE = object()
_RUNNING = object()
_KILLED = object()

_COLORS = {
    "red": [31, 0],
    "green": [32, 0],
    "yellow": [33, 0],
    "lightgray": [37, 0],
    "gray": [30, 1],
    "purple": [35, 0],
    "cyan": [36, 0],
}

_BEGINNING_OF_LINE = "\x1b[0G"
_CLEAR_LINE = "\x1b[2K"

_TAG_COLOR = {
    "FAILED": "red",
    "FLAKE": "purple",
    "TIMEOUT_FLAKE": "purple",
    "WARNING": "yellow",
    "TIMEOUT": "red",
    "PASSED": "green",
    "START": "gray",
    "WAITING": "yellow",
    "SUCCESS": "green",
    "IDLE": "gray",
    "SKIPPED": "cyan",
}

_FORMAT = "%(asctime)-15s %(message)s"
logging.basicConfig(level=logging.INFO, format=_FORMAT)


def eintr_be_gone(fn):
    """Run fn until it doesn't stop because of EINTR"""
    while True:
        try:
            return fn()
        except IOError as e:
            if e.errno != errno.EINTR:
                raise


def message(tag, msg, explanatory_text=None, do_newline=False):
    if (
        message.old_tag == tag
        and message.old_msg == msg
        and not explanatory_text
    ):
        return
    message.old_tag = tag
    message.old_msg = msg
    if explanatory_text:
        if isinstance(explanatory_text, bytes):
            explanatory_text = explanatory_text.decode("utf8", errors="replace")
    while True:
        try:
            if platform_string() == "windows" or not sys.stdout.isatty():
                if explanatory_text:
                    logging.info(explanatory_text)
                logging.info("%s: %s", tag, msg)
            else:
                sys.stdout.write(
                    "%s%s%s\x1b[%d;%dm%s\x1b[0m: %s%s"
                    % (
                        _BEGINNING_OF_LINE,
                        _CLEAR_LINE,
                        "\n%s" % explanatory_text
                        if explanatory_text is not None
                        else "",
                        _COLORS[_TAG_COLOR[tag]][1],
                        _COLORS[_TAG_COLOR[tag]][0],
                        tag,
                        msg,
                        "\n"
                        if do_newline or explanatory_text is not None
                        else "",
                    )
                )
            sys.stdout.flush()
            return
        except IOError as e:
            if e.errno != errno.EINTR:
                raise


message.old_tag = ""
message.old_msg = ""


def which(filename):
    if "/" in filename:
        return filename
    for path in os.environ["PATH"].split(os.pathsep):
        if os.path.exists(os.path.join(path, filename)):
            return os.path.join(path, filename)
    raise Exception("%s not found" % filename)


class JobSpec(object):
    """Specifies what to run for a job."""

    def __init__(
        self,
        cmdline,
        shortname=None,
        environ=None,
        cwd=None,
        shell=False,
        timeout_seconds=5 * 60,
        flake_retries=0,
        timeout_retries=0,
        kill_handler=None,
        cpu_cost=1.0,
        verbose_success=False,
        logfilename=None,
    ):
        """
        Arguments:
          cmdline: a list of arguments to pass as the command line
          environ: a dictionary of environment variables to set in the child process
          kill_handler: a handler that will be called whenever job.kill() is invoked
          cpu_cost: number of cores per second this job needs
          logfilename: use given file to store job's output, rather than using a temporary file
        """
        if environ is None:
            environ = {}
        self.cmdline = cmdline
        self.environ = environ
        self.shortname = cmdline[0] if shortname is None else shortname
        self.cwd = cwd
        self.shell = shell
        self.timeout_seconds = timeout_seconds
        self.flake_retries = flake_retries
        self.timeout_retries = timeout_retries
        self.kill_handler = kill_handler
        self.cpu_cost = cpu_cost
        self.verbose_success = verbose_success
        self.logfilename = logfilename
        if (
            self.logfilename
            and self.flake_retries != 0
            and self.timeout_retries != 0
        ):
            # Forbidden to avoid overwriting the test log when retrying.
            raise Exception(
                "Cannot use custom logfile when retries are enabled"
            )

    def identity(self):
        return "%r %r" % (self.cmdline, self.environ)

    def __hash__(self):
        return hash(self.identity())

    def __cmp__(self, other):
        return self.identity() == other.identity()

    def __lt__(self, other):
        return self.identity() < other.identity()

    def __repr__(self):
        return "JobSpec(shortname=%s, cmdline=%s)" % (
            self.shortname,
            self.cmdline,
        )

    def __str__(self):
        return "%s: %s %s" % (
            self.shortname,
            " ".join("%s=%s" % kv for kv in list(self.environ.items())),
            " ".join(self.cmdline),
        )


class JobResult(object):
    def __init__(self):
        self.state = "UNKNOWN"
        self.returncode = -1
        self.elapsed_time = 0
        self.num_failures = 0
        self.retries = 0
        self.message = ""
        self.cpu_estimated = 1
        self.cpu_measured = 1


def read_from_start(f):
    f.seek(0)
    return f.read()


class Job(object):
    """Manages one job."""

    def __init__(
        self, spec, newline_on_success, travis, add_env, quiet_success=False
    ):
        self._spec = spec
        self._newline_on_success = newline_on_success
        self._travis = travis
        self._add_env = add_env.copy()
        self._retries = 0
        self._timeout_retries = 0
        self._suppress_failure_message = False
        self._quiet_success = quiet_success
        if not self._quiet_success:
            message("START", spec.shortname, do_newline=self._travis)
        self.result = JobResult()
        self.start()

    def GetSpec(self):
        return self._spec

    def start(self):
        if self._spec.logfilename:
            # make sure the log directory exists
            logfile_dir = os.path.dirname(
                os.path.abspath(self._spec.logfilename)
            )
            if not os.path.exists(logfile_dir):
                os.makedirs(logfile_dir)
            self._logfile = open(self._spec.logfilename, "w+")
        else:
            # macOS: a series of quick os.unlink invocation might cause OS
            # error during the creation of temporary file. By using
            # NamedTemporaryFile, we defer the removal of file and directory.
            self._logfile = open("/tmp/temp.log", "w+")
        env = dict(os.environ)
        env.update(self._spec.environ)
        env.update(self._add_env)
        env = sanitized_environment(env)
        self._start = time.time()
        cmdline = self._spec.cmdline
        # The Unix time command is finicky when used with MSBuild, so we don't use it
        # with jobs that run MSBuild.
        global measure_cpu_costs
        if measure_cpu_costs and not "vsprojects\\build" in cmdline[0]:
            cmdline = ["time", "-p"] + cmdline
        else:
            measure_cpu_costs = False
        print(self._spec.logfilename)
        try_start = lambda: subprocess.Popen(
            args=cmdline,
            stderr=subprocess.STDOUT,
            stdout=self._logfile,
            cwd=self._spec.cwd,
            shell=self._spec.shell,
            env=env,
        )
        delay = 0.3
        for i in range(0, 4):
            try:
                self._process = try_start()
                break
            except OSError:
                message(
                    "WARNING",
                    "Failed to start %s, retrying in %f seconds"
                    % (self._spec.shortname, delay),
                )
                time.sleep(delay)
                delay *= 2
        else:
            self._process = try_start()
        self._state = _RUNNING

    def state(self):
        """Poll current state of the job. Prints messages at completion."""

        def stdout(self=self):
            stdout = read_from_start(self._logfile)
            self.result.message = stdout[-_MAX_RESULT_SIZE:]
            return stdout

        if self._state == _RUNNING and self._process.poll() is not None:
            elapsed = time.time() - self._start
            self.result.elapsed_time = elapsed
            if self._process.returncode != 0:
                if self._retries < self._spec.flake_retries:
                    message(
                        "FLAKE",
                        "%s [ret=%d, pid=%d]"
                        % (
                            self._spec.shortname,
                            self._process.returncode,
                            self._process.pid,
                        ),
                        stdout(),
                        do_newline=True,
                    )
                    self._retries += 1
                    self.result.num_failures += 1
                    self.result.retries = self._timeout_retries + self._retries
                    # NOTE: job is restarted regardless of jobset's max_time setting
                    self.start()
                else:
                    self._state = _FAILURE
                    if not self._suppress_failure_message:
                        message(
                            "FAILED",
                            "%s [ret=%d, pid=%d, time=%.1fsec]"
                            % (
                                self._spec.shortname,
                                self._process.returncode,
                                self._process.pid,
                                elapsed,
                            ),
                            stdout(),
                            do_newline=True,
                        )
                    self.result.state = "FAILED"
                    self.result.num_failures += 1
                    self.result.returncode = self._process.returncode
            else:
                self._state = _SUCCESS
                measurement = ""
                if measure_cpu_costs:
                    m = re.search(
                        r"real\s+([0-9.]+)\nuser\s+([0-9.]+)\nsys\s+([0-9.]+)",
                        (stdout()).decode("utf8", errors="replace"),
                    )
                    real = float(m.group(1))
                    user = float(m.group(2))
                    sys = float(m.group(3))
                    if real > 0.5:
                        cores = (user + sys) / real
                        self.result.cpu_measured = float("%.01f" % cores)
                        self.result.cpu_estimated = float(
                            "%.01f" % self._spec.cpu_cost
                        )
                        measurement = "; cpu_cost=%.01f; estimated=%.01f" % (
                            self.result.cpu_measured,
                            self.result.cpu_estimated,
                        )
                if not self._quiet_success:
                    message(
                        "PASSED",
                        "%s [time=%.1fsec, retries=%d:%d%s]"
                        % (
                            self._spec.shortname,
                            elapsed,
                            self._retries,
                            self._timeout_retries,
                            measurement,
                        ),
                        stdout() if self._spec.verbose_success else None,
                        do_newline=self._newline_on_success or self._travis,
                    )
                self.result.state = "PASSED"
        elif (
            self._state == _RUNNING
            and self._spec.timeout_seconds is not None
            and time.time() - self._start > self._spec.timeout_seconds
        ):
            elapsed = time.time() - self._start
            self.result.elapsed_time = elapsed
            if self._timeout_retries < self._spec.timeout_retries:
                message(
                    "TIMEOUT_FLAKE",
                    "%s [pid=%d]" % (self._spec.shortname, self._process.pid),
                    stdout(),
                    do_newline=True,
                )
                self._timeout_retries += 1
                self.result.num_failures += 1
                self.result.retries = self._timeout_retries + self._retries
                if self._spec.kill_handler:
                    self._spec.kill_handler(self)
                self._process.terminate()
                # NOTE: job is restarted regardless of jobset's max_time setting
                self.start()
            else:
                message(
                    "TIMEOUT",
                    "%s [pid=%d, time=%.1fsec]"
                    % (self._spec.shortname, self._process.pid, elapsed),
                    stdout(),
                    do_newline=True,
                )
                self.kill()
                self.result.state = "TIMEOUT"
                self.result.num_failures += 1
        return self._state

    def kill(self):
        if self._state == _RUNNING:
            self._state = _KILLED
            if self._spec.kill_handler:
                self._spec.kill_handler(self)
            self._process.terminate()

    def suppress_failure_message(self):
        self._suppress_failure_message = True


class Jobset(object):
    """Manages one run of jobs."""

    def __init__(
        self,
        check_cancelled,
        maxjobs,
        maxjobs_cpu_agnostic,
        newline_on_success,
        travis,
        stop_on_failure,
        add_env,
        quiet_success,
        max_time,
    ):
        self._running = set()
        self._check_cancelled = check_cancelled
        self._cancelled = False
        self._failures = 0
        self._completed = 0
        self._maxjobs = maxjobs
        self._maxjobs_cpu_agnostic = maxjobs_cpu_agnostic
        self._newline_on_success = newline_on_success
        self._travis = travis
        self._stop_on_failure = stop_on_failure
        self._add_env = add_env
        self._quiet_success = quiet_success
        self._max_time = max_time
        self.resultset = {}
        self._remaining = None
        self._start_time = time.time()

    def set_remaining(self, remaining):
        self._remaining = remaining

    def get_num_failures(self):
        return self._failures

    def cpu_cost(self):
        c = 0
        for job in self._running:
            c += job._spec.cpu_cost
        return c

    def start(self, spec):
        """Start a job. Return True on success, False on failure."""
        while True:
            if (
                self._max_time > 0
                and time.time() - self._start_time > self._max_time
            ):
                skipped_job_result = JobResult()
                skipped_job_result.state = "SKIPPED"
                message("SKIPPED", spec.shortname, do_newline=True)
                self.resultset[spec.shortname] = [skipped_job_result]
                return True
            if self.cancelled():
                return False
            current_cpu_cost = self.cpu_cost()
            if current_cpu_cost == 0:
                break
            if current_cpu_cost + spec.cpu_cost <= self._maxjobs:
                if len(self._running) < self._maxjobs_cpu_agnostic:
                    break
            self.reap(spec.shortname, spec.cpu_cost)
        if self.cancelled():
            return False
        job = Job(
            spec,
            self._newline_on_success,
            self._travis,
            self._add_env,
            self._quiet_success,
        )
        self._running.add(job)
        if job.GetSpec().shortname not in self.resultset:
            self.resultset[job.GetSpec().shortname] = []
        return True

    def reap(self, waiting_for=None, waiting_for_cost=None):
        """Collect the dead jobs."""
        while self._running:
            dead = set()
            for job in self._running:
                st = eintr_be_gone(lambda: job.state())
                if st == _RUNNING:
                    continue
                if st == _FAILURE or st == _KILLED:
                    self._failures += 1
                    if self._stop_on_failure:
                        self._cancelled = True
                        for job in self._running:
                            job.kill()
                dead.add(job)
                break
            for job in dead:
                self._completed += 1
                if not self._quiet_success or job.result.state != "PASSED":
                    self.resultset[job.GetSpec().shortname].append(job.result)
                self._running.remove(job)
            if dead:
                return
            if not self._travis and platform_string() != "windows":
                rstr = (
                    ""
                    if self._remaining is None
                    else "%d queued, " % self._remaining
                )
                if self._remaining is not None and self._completed > 0:
                    now = time.time()
                    sofar = now - self._start_time
                    remaining = (
                        sofar
                        / self._completed
                        * (self._remaining + len(self._running))
                    )
                    rstr = "ETA %.1f sec; %s" % (remaining, rstr)
                if waiting_for is not None:
                    wstr = " next: %s @ %.2f cpu" % (
                        waiting_for,
                        waiting_for_cost,
                    )
                else:
                    wstr = ""
                message(
                    "WAITING",
                    "%s%d jobs running, %d complete, %d failed (load %.2f)%s"
                    % (
                        rstr,
                        len(self._running),
                        self._completed,
                        self._failures,
                        self.cpu_cost(),
                        wstr,
                    ),
                )
            if platform_string() == "windows":
                time.sleep(0.1)
            else:
                signal.alarm(10)
                signal.pause()

    def cancelled(self):
        """Poll for cancellation."""
        if self._cancelled:
            return True
        if not self._check_cancelled():
            return False
        for job in self._running:
            job.kill()
        self._cancelled = True
        return True

    def finish(self):
        while self._running:
            if self.cancelled():
                pass  # poll cancellation
            self.reap()
        if platform_string() != "windows":
            signal.alarm(0)
        return not self.cancelled() and self._failures == 0


def _never_cancelled():
    return False


def tag_remaining(xs):
    staging = []
    for x in xs:
        staging.append(x)
        if len(staging) > 5000:
            yield (staging.pop(0), None)
    n = len(staging)
    for i, x in enumerate(staging):
        yield (x, n - i - 1)


def run(
    cmdlines,
    check_cancelled=_never_cancelled,
    maxjobs=None,
    maxjobs_cpu_agnostic=None,
    newline_on_success=False,
    travis=False,
    infinite_runs=False,
    stop_on_failure=False,
    add_env={},
    skip_jobs=False,
    quiet_success=False,
    max_time=-1,
):
    if skip_jobs:
        resultset = {}
        skipped_job_result = JobResult()
        skipped_job_result.state = "SKIPPED"
        for job in cmdlines:
            message("SKIPPED", job.shortname, do_newline=True)
            resultset[job.shortname] = [skipped_job_result]
        return 0, resultset
    js = Jobset(
        check_cancelled,
        maxjobs if maxjobs is not None else _DEFAULT_MAX_JOBS,
        maxjobs_cpu_agnostic
        if maxjobs_cpu_agnostic is not None
        else _DEFAULT_MAX_JOBS,
        newline_on_success,
        travis,
        stop_on_failure,
        add_env,
        quiet_success,
        max_time,
    )
    for cmdline, remaining in tag_remaining(cmdlines):
        if not js.start(cmdline):
            break
        if remaining is not None:
            js.set_remaining(remaining)
    js.finish()
    return js.get_num_failures(), js.resultset
