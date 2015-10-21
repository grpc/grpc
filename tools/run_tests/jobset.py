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

import hashlib
import multiprocessing
import os
import platform
import signal
import string
import subprocess
import sys
import tempfile
import time
import xml.etree.cElementTree as ET


_DEFAULT_MAX_JOBS = 16 * multiprocessing.cpu_count()


# setup a signal handler so that signal.pause registers 'something'
# when a child finishes
# not using futures and threading to avoid a dependency on subprocess32
if platform.system() == "Windows":
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
    if platform.system() == 'Windows' or not sys.stdout.isatty():
      if explanatory_text:
        print explanatory_text
      print '%s: %s' % (tag, msg)
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

message.old_tag = ""
message.old_msg = ""

def which(filename):
  if '/' in filename:
    return filename
  for path in os.environ['PATH'].split(os.pathsep):
    if os.path.exists(os.path.join(path, filename)):
      return os.path.join(path, filename)
  raise Exception('%s not found' % filename)


def _filter_stdout(stdout):
  """Filters out nonprintable and XML-illegal characters from stdout."""
  # keep whitespaces but remove formfeed and vertical tab characters
  # that make XML report unparseable.
  return filter(lambda x: x in string.printable and x != '\f' and x != '\v',
                stdout.decode(errors='ignore'))


class JobSpec(object):
  """Specifies what to run for a job."""

  def __init__(self, cmdline, shortname=None, environ=None, hash_targets=None,
               cwd=None, shell=False, timeout_seconds=5*60, flake_retries=0,
               timeout_retries=0, kill_handler=None):
    """
    Arguments:
      cmdline: a list of arguments to pass as the command line
      environ: a dictionary of environment variables to set in the child process
      hash_targets: which files to include in the hash representing the jobs version
                    (or empty, indicating the job should not be hashed)
      kill_handler: a handler that will be called whenever job.kill() is invoked
    """
    if environ is None:
      environ = {}
    if hash_targets is None:
      hash_targets = []
    self.cmdline = cmdline
    self.environ = environ
    self.shortname = cmdline[0] if shortname is None else shortname
    self.hash_targets = hash_targets or []
    self.cwd = cwd
    self.shell = shell
    self.timeout_seconds = timeout_seconds
    self.flake_retries = flake_retries
    self.timeout_retries = timeout_retries
    self.kill_handler = kill_handler

  def identity(self):
    return '%r %r %r' % (self.cmdline, self.environ, self.hash_targets)

  def __hash__(self):
    return hash(self.identity())

  def __cmp__(self, other):
    return self.identity() == other.identity()


class Job(object):
  """Manages one job."""

  def __init__(self, spec, bin_hash, newline_on_success, travis, add_env, xml_report):
    self._spec = spec
    self._bin_hash = bin_hash
    self._newline_on_success = newline_on_success
    self._travis = travis
    self._add_env = add_env.copy()
    self._xml_test = ET.SubElement(xml_report, 'testcase',
                                   name=self._spec.shortname) if xml_report is not None else None
    self._retries = 0
    self._timeout_retries = 0
    self._suppress_failure_message = False
    message('START', spec.shortname, do_newline=self._travis)
    self.start()

  def start(self):
    self._tempfile = tempfile.TemporaryFile()
    env = dict(os.environ)
    env.update(self._spec.environ)
    env.update(self._add_env)
    self._start = time.time()
    self._process = subprocess.Popen(args=self._spec.cmdline,
                                     stderr=subprocess.STDOUT,
                                     stdout=self._tempfile,
                                     cwd=self._spec.cwd,
                                     shell=self._spec.shell,
                                     env=env)
    self._state = _RUNNING

  def state(self, update_cache):
    """Poll current state of the job. Prints messages at completion."""
    if self._state == _RUNNING and self._process.poll() is not None:
      elapsed = time.time() - self._start
      self._tempfile.seek(0)
      stdout = self._tempfile.read()
      filtered_stdout = _filter_stdout(stdout)
      # TODO: looks like jenkins master is slow because parsing the junit results XMLs is not
      # implemented efficiently. This is an experiment to workaround the issue by making sure
      # results.xml file is small enough.
      filtered_stdout = filtered_stdout[-128:]
      if self._xml_test is not None:
        self._xml_test.set('time', str(elapsed))
        ET.SubElement(self._xml_test, 'system-out').text = filtered_stdout
      if self._process.returncode != 0:
        if self._retries < self._spec.flake_retries:
          message('FLAKE', '%s [ret=%d, pid=%d]' % (
            self._spec.shortname, self._process.returncode, self._process.pid),
            stdout, do_newline=True)
          self._retries += 1
          self.start()
        else:
          self._state = _FAILURE
          if not self._suppress_failure_message:
            message('FAILED', '%s [ret=%d, pid=%d]' % (
                self._spec.shortname, self._process.returncode, self._process.pid),
                stdout, do_newline=True)
          if self._xml_test is not None:
            ET.SubElement(self._xml_test, 'failure', message='Failure')
      else:
        self._state = _SUCCESS
        message('PASSED', '%s [time=%.1fsec; retries=%d;%d]' % (
                    self._spec.shortname, elapsed, self._retries, self._timeout_retries),
            do_newline=self._newline_on_success or self._travis)
        if self._bin_hash:
          update_cache.finished(self._spec.identity(), self._bin_hash)
    elif self._state == _RUNNING and time.time() - self._start > self._spec.timeout_seconds:
      self._tempfile.seek(0)
      stdout = self._tempfile.read()
      filtered_stdout = _filter_stdout(stdout)
      if self._timeout_retries < self._spec.timeout_retries:
        message('TIMEOUT_FLAKE', self._spec.shortname, stdout, do_newline=True)
        self._timeout_retries += 1
        self._process.terminate()
        self.start()
      else:
        message('TIMEOUT', self._spec.shortname, stdout, do_newline=True)
        self.kill()
        if self._xml_test is not None:
          ET.SubElement(self._xml_test, 'system-out').text = filtered_stdout
          ET.SubElement(self._xml_test, 'error', message='Timeout')
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
               stop_on_failure, add_env, cache, xml_report):
    self._running = set()
    self._check_cancelled = check_cancelled
    self._cancelled = False
    self._failures = 0
    self._completed = 0
    self._maxjobs = maxjobs
    self._newline_on_success = newline_on_success
    self._travis = travis
    self._cache = cache
    self._stop_on_failure = stop_on_failure
    self._hashes = {}
    self._xml_report = xml_report
    self._add_env = add_env

  def start(self, spec):
    """Start a job. Return True on success, False on failure."""
    while len(self._running) >= self._maxjobs:
      if self.cancelled(): return False
      self.reap()
    if self.cancelled(): return False
    if spec.hash_targets:
      if spec.identity() in self._hashes:
        bin_hash = self._hashes[spec.identity()]
      else:
        bin_hash = hashlib.sha1()
        for fn in spec.hash_targets:
          with open(which(fn)) as f:
            bin_hash.update(f.read())
        bin_hash = bin_hash.hexdigest()
        self._hashes[spec.identity()] = bin_hash
      should_run = self._cache.should_run(spec.identity(), bin_hash)
    else:
      bin_hash = None
      should_run = True
    if should_run:
      self._running.add(Job(spec,
                            bin_hash,
                            self._newline_on_success,
                            self._travis,
                            self._add_env,
                            self._xml_report))
    return True

  def reap(self):
    """Collect the dead jobs."""
    while self._running:
      dead = set()
      for job in self._running:
        st = job.state(self._cache)
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
        self._running.remove(job)
      if dead: return
      if (not self._travis):
        message('WAITING', '%d jobs running, %d complete, %d failed' % (
            len(self._running), self._completed, self._failures))
      if platform.system() == 'Windows':
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


# cache class that caches nothing
class NoCache(object):
  def should_run(self, cmdline, bin_hash):
    return True

  def finished(self, cmdline, bin_hash):
    pass


def run(cmdlines,
        check_cancelled=_never_cancelled,
        maxjobs=None,
        newline_on_success=False,
        travis=False,
        infinite_runs=False,
        stop_on_failure=False,
        cache=None,
        xml_report=None,
        add_env={}):
  js = Jobset(check_cancelled,
              maxjobs if maxjobs is not None else _DEFAULT_MAX_JOBS,
              newline_on_success, travis, stop_on_failure, add_env,
              cache if cache is not None else NoCache(),
              xml_report)
  for cmdline in cmdlines:
    if not js.start(cmdline):
      break
  return js.finish()
