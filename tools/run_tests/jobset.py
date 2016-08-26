# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Run a group of subprocesses and then finish."""

from __future__ import print_function

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
_MAX_RESULT_SIZE = 8192

def sanitized_environment(env):
  sanitized = {}
  for key, value in env.items():
    sanitized[str(key).encode()] = str(value).encode()
  return sanitized

def platform_string():
  if platform.system() == 'Windows':
    return 'windows'
  elif platform.system()[:7] == 'MSYS_NT':
    return 'windows'
  elif platform.system() == 'Darwin':
    return 'mac'
  elif platform.system() == 'Linux':
    return 'linux'
  else:
    return 'posix'


# setup a signal handler so that signal.pause registers 'something'
# when a child finishes
# not using futures and threading to avoid a dependency on subprocess32
if platform_string() == 'windows':
  pass
else:
  have_alarm = False
  def alarm_handler(unused_signum, unused_frame):
    global have_alarm
    have_alarm = False

  signal.signal(signal.SIGCHLD, lambda unused_signum, unused_frame: None)
  signal.signal(signal.SIGALRM, alarm_handler)


_SUCCESS = object()
_FAILURE = object()
_RUNNING = object()
_KILLED = object()


_COLORS = {
    'red': [ 31, 0 ],
    'green': [ 32, 0 ],
    'yellow': [ 33, 0 ],
    'lightgray': [ 37, 0],
    'gray': [ 30, 1 ],
    'purple': [ 35, 0 ],
    }


_BEGINNING_OF_LINE = '\x1b[0G'
_CLEAR_LINE = '\x1b[2K'


_TAG_COLOR = {
    'FAILED': 'red',
    'FLAKE': 'purple',
    'TIMEOUT_FLAKE': 'purple',
    'WARNING': 'yellow',
    'TIMEOUT': 'red',
    'PASSED': 'green',
    'START': 'gray',
    'WAITING': 'yellow',
    'SUCCESS': 'green',
    'IDLE': 'gray',
    }


def message(tag, msg, explanatory_text=None, do_newline=False):
  if message.old_tag == tag and message.old_msg == msg and not explanatory_text:
    return
  message.old_tag = tag
  message.old_msg = msg
  try:
    if platform_string() == 'windows' or not sys.stdout.isatty():
      if explanatory_text:
        print(explanatory_text)
      print('%s: %s' % (tag, msg))
      return
    sys.stdout.write('%s%s%s\x1b[%d;%dm%s\x1b[0m: %s%s' % (
        _BEGINNING_OF_LINE,
        _CLEAR_LINE,
        '\n%s' % explanatory_text if explanatory_text is not None else '',
        _COLORS[_TAG_COLOR[tag]][1],
        _COLORS[_TAG_COLOR[tag]][0],
        tag,
        msg,
        '\n' if do_newline or explanatory_text is not None else ''))
    sys.stdout.flush()
  except:
    pass

message.old_tag = ''
message.old_msg = ''

def which(filename):
  if '/' in filename:
    return filename
  for path in os.environ['PATH'].split(os.pathsep):
    if os.path.exists(os.path.join(path, filename)):
      return os.path.join(path, filename)
  raise Exception('%s not found' % filename)


class JobSpec(object):
  """Specifies what to run for a job."""

  def __init__(self, cmdline, shortname=None, environ=None,
               cwd=None, shell=False, timeout_seconds=5*60, flake_retries=0,
               timeout_retries=0, kill_handler=None, cpu_cost=1.0,
               verbose_success=False):
    """
    Arguments:
      cmdline: a list of arguments to pass as the command line
      environ: a dictionary of environment variables to set in the child process
      kill_handler: a handler that will be called whenever job.kill() is invoked
      cpu_cost: number of cores per second this job needs
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

  def identity(self):
    return '%r %r' % (self.cmdline, self.environ)

  def __hash__(self):
    return hash(self.identity())

  def __cmp__(self, other):
    return self.identity() == other.identity()

  def __repr__(self):
    return 'JobSpec(shortname=%s, cmdline=%s)' % (self.shortname, self.cmdline)


class JobResult(object):
  def __init__(self):
    self.state = 'UNKNOWN'
    self.returncode = -1
    self.elapsed_time = 0
    self.num_failures = 0
    self.retries = 0
    self.message = ''


class Job(object):
  """Manages one job."""

  def __init__(self, spec, newline_on_success, travis, add_env):
    self._spec = spec
    self._newline_on_success = newline_on_success
    self._travis = travis
    self._add_env = add_env.copy()
    self._retries = 0
    self._timeout_retries = 0
    self._suppress_failure_message = False
    message('START', spec.shortname, do_newline=self._travis)
    self.result = JobResult()
    self.start()

  def GetSpec(self):
    return self._spec

  def start(self):
    self._tempfile = tempfile.TemporaryFile()
    env = dict(os.environ)
    env.update(self._spec.environ)
    env.update(self._add_env)
    env = sanitized_environment(env)
    self._start = time.time()
    cmdline = self._spec.cmdline
    if measure_cpu_costs:
      cmdline = ['time', '--portability'] + cmdline
    try_start = lambda: subprocess.Popen(args=cmdline,
                                         stderr=subprocess.STDOUT,
                                         stdout=self._tempfile,
                                         cwd=self._spec.cwd,
                                         shell=self._spec.shell,
                                         env=env)
    delay = 0.3
    for i in range(0, 4):
      try:
        self._process = try_start()
        break
      except OSError:
        message('WARNING', 'Failed to start %s, retrying in %f seconds' % (self._spec.shortname, delay))
        time.sleep(delay)
        delay *= 2
    else:
      self._process = try_start()
    self._state = _RUNNING

  def state(self):
    """Poll current state of the job. Prints messages at completion."""
    def stdout(self=self):
      self._tempfile.seek(0)
      stdout = self._tempfile.read()
      self.result.message = stdout[-_MAX_RESULT_SIZE:]
      return stdout
    if self._state == _RUNNING and self._process.poll() is not None:
      elapsed = time.time() - self._start
      self.result.elapsed_time = elapsed
      if self._process.returncode != 0:
        if self._retries < self._spec.flake_retries:
          message('FLAKE', '%s [ret=%d, pid=%d]' % (
            self._spec.shortname, self._process.returncode, self._process.pid),
            stdout(), do_newline=True)
          self._retries += 1
          self.result.num_failures += 1
          self.result.retries = self._timeout_retries + self._retries
          self.start()
        else:
          self._state = _FAILURE
          if not self._suppress_failure_message:
            message('FAILED', '%s [ret=%d, pid=%d]' % (
                self._spec.shortname, self._process.returncode, self._process.pid),
                stdout(), do_newline=True)
          self.result.state = 'FAILED'
          self.result.num_failures += 1
          self.result.returncode = self._process.returncode
      else:
        self._state = _SUCCESS
        measurement = ''
        if measure_cpu_costs:
          m = re.search(r'real ([0-9.]+)\nuser ([0-9.]+)\nsys ([0-9.]+)', stdout())
          real = float(m.group(1))
          user = float(m.group(2))
          sys = float(m.group(3))
          if real > 0.5:
            cores = (user + sys) / real
            measurement = '; cpu_cost=%.01f; estimated=%.01f' % (cores, self._spec.cpu_cost)
        message('PASSED', '%s [time=%.1fsec; retries=%d:%d%s]' % (
            self._spec.shortname, elapsed, self._retries, self._timeout_retries, measurement),
            stdout() if self._spec.verbose_success else None,
            do_newline=self._newline_on_success or self._travis)
        self.result.state = 'PASSED'
    elif (self._state == _RUNNING and
          self._spec.timeout_seconds is not None and
          time.time() - self._start > self._spec.timeout_seconds):
      if self._timeout_retries < self._spec.timeout_retries:
        message('TIMEOUT_FLAKE', '%s [pid=%d]' % (self._spec.shortname, self._process.pid), stdout(), do_newline=True)
        self._timeout_retries += 1
        self.result.num_failures += 1
        self.result.retries = self._timeout_retries + self._retries
        if self._spec.kill_handler:
          self._spec.kill_handler(self)
        self._process.terminate()
        self.start()
      else:
        message('TIMEOUT', '%s [pid=%d]' % (self._spec.shortname, self._process.pid), stdout(), do_newline=True)
        self.kill()
        self.result.state = 'TIMEOUT'
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

  def __init__(self, check_cancelled, maxjobs, newline_on_success, travis,
               stop_on_failure, add_env):
    self._running = set()
    self._check_cancelled = check_cancelled
    self._cancelled = False
    self._failures = 0
    self._completed = 0
    self._maxjobs = maxjobs
    self._newline_on_success = newline_on_success
    self._travis = travis
    self._stop_on_failure = stop_on_failure
    self._add_env = add_env
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
      if self.cancelled(): return False
      current_cpu_cost = self.cpu_cost()
      if current_cpu_cost == 0: break
      if current_cpu_cost + spec.cpu_cost <= self._maxjobs: break
      self.reap()
    if self.cancelled(): return False
    job = Job(spec,
              self._newline_on_success,
              self._travis,
              self._add_env)
    self._running.add(job)
    if job.GetSpec().shortname not in self.resultset:
      self.resultset[job.GetSpec().shortname] = []
    return True

  def reap(self):
    """Collect the dead jobs."""
    while self._running:
      dead = set()
      for job in self._running:
        st = job.state()
        if st == _RUNNING: continue
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
        self.resultset[job.GetSpec().shortname].append(job.result)
        self._running.remove(job)
      if dead: return
      if (not self._travis):
        rstr = '' if self._remaining is None else '%d queued, ' % self._remaining
        if self._remaining is not None and self._completed > 0:
          now = time.time()
          sofar = now - self._start_time
          remaining = sofar / self._completed * (self._remaining + len(self._running))
          rstr = 'ETA %.1f sec; %s' % (remaining, rstr)
        message('WAITING', '%s%d jobs running, %d complete, %d failed' % (
            rstr, len(self._running), self._completed, self._failures))
      if platform_string() == 'windows':
        time.sleep(0.1)
      else:
        global have_alarm
        if not have_alarm:
          have_alarm = True
          signal.alarm(10)
        signal.pause()

  def cancelled(self):
    """Poll for cancellation."""
    if self._cancelled: return True
    if not self._check_cancelled(): return False
    for job in self._running:
      job.kill()
    self._cancelled = True
    return True

  def finish(self):
    while self._running:
      if self.cancelled(): pass  # poll cancellation
      self.reap()
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


def run(cmdlines,
        check_cancelled=_never_cancelled,
        maxjobs=None,
        newline_on_success=False,
        travis=False,
        infinite_runs=False,
        stop_on_failure=False,
        add_env={}):
  js = Jobset(check_cancelled,
              maxjobs if maxjobs is not None else _DEFAULT_MAX_JOBS,
              newline_on_success, travis, stop_on_failure, add_env)
  for cmdline, remaining in tag_remaining(cmdlines):
    if not js.start(cmdline):
      break
    if remaining is not None:
      js.set_remaining(remaining)
  js.finish()
  return js.get_num_failures(), js.resultset
