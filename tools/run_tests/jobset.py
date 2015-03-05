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
import random
import signal
import subprocess
import sys
import tempfile
import time


_DEFAULT_MAX_JOBS = 16 * multiprocessing.cpu_count()


have_alarm = False
def alarm_handler(unused_signum, unused_frame):
  global have_alarm
  have_alarm = False


# setup a signal handler so that signal.pause registers 'something'
# when a child finishes
# not using futures and threading to avoid a dependency on subprocess32
signal.signal(signal.SIGCHLD, lambda unused_signum, unused_frame: None)
signal.signal(signal.SIGALRM, alarm_handler)


def shuffle_iteratable(it):
  """Return an iterable that randomly walks it"""
  # take a random sampling from the passed in iterable
  # we take an element with probablity 1/p and rapidly increase
  # p as we take elements - this gives us a somewhat random set of values before
  # we've seen all the values, but starts producing values without having to
  # compute ALL of them at once, allowing tests to start a little earlier
  nextit = []
  p = 1
  for val in it:
    if random.randint(0, p) == 0:
      p = min(p*2, 100)
      yield val
    else:
      nextit.append(val)
  # after taking a random sampling, we shuffle the rest of the elements and
  # yield them
  random.shuffle(nextit)
  for val in nextit:
    yield val


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
    }


_BEGINNING_OF_LINE = '\x1b[0G'
_CLEAR_LINE = '\x1b[2K'


_TAG_COLOR = {
    'FAILED': 'red',
    'TIMEOUT': 'red',
    'PASSED': 'green',
    'START': 'gray',
    'WAITING': 'yellow',
    'SUCCESS': 'green',
    'IDLE': 'gray',
    }


def message(tag, message, explanatory_text=None, do_newline=False):
  try:
    sys.stdout.write('%s%s%s\x1b[%d;%dm%s\x1b[0m: %s%s' % (
        _BEGINNING_OF_LINE,
        _CLEAR_LINE,
        '\n%s' % explanatory_text if explanatory_text is not None else '',
        _COLORS[_TAG_COLOR[tag]][1],
        _COLORS[_TAG_COLOR[tag]][0],
        tag,
        message,
        '\n' if do_newline or explanatory_text is not None else ''))
    sys.stdout.flush()
  except:
    pass


def which(filename):
  if '/' in filename:
    return filename
  for path in os.environ['PATH'].split(os.pathsep):
    if os.path.exists(os.path.join(path, filename)):
      return os.path.join(path, filename)
  raise Exception('%s not found' % filename)


class JobSpec(object):
  """Specifies what to run for a job."""

  def __init__(self, cmdline, shortname=None, environ=None, hash_targets=None):
    """
    Arguments:
      cmdline: a list of arguments to pass as the command line
      environ: a dictionary of environment variables to set in the child process
      hash_targets: which files to include in the hash representing the jobs version
                    (or empty, indicating the job should not be hashed)
    """
    if environ is None:
      environ = {}
    if hash_targets is None:
      hash_targets = []
    self.cmdline = cmdline
    self.environ = environ
    self.shortname = cmdline[0] if shortname is None else shortname
    self.hash_targets = hash_targets or []

  def identity(self):
    return '%r %r %r' % (self.cmdline, self.environ, self.hash_targets)

  def __hash__(self):
    return hash(self.identity())

  def __cmp__(self, other):
    return self.identity() == other.identity()


class Job(object):
  """Manages one job."""

  def __init__(self, spec, bin_hash, newline_on_success, travis):
    self._spec = spec
    self._bin_hash = bin_hash
    self._tempfile = tempfile.TemporaryFile()
    env = os.environ.copy()
    for k, v in spec.environ.iteritems():
      env[k] = v
    self._start = time.time()
    self._process = subprocess.Popen(args=spec.cmdline,
                                     stderr=subprocess.STDOUT,
                                     stdout=self._tempfile,
                                     env=env)
    self._state = _RUNNING
    self._newline_on_success = newline_on_success
    self._travis = travis
    message('START', spec.shortname, do_newline=self._travis)

  def state(self, update_cache):
    """Poll current state of the job. Prints messages at completion."""
    if self._state == _RUNNING and self._process.poll() is not None:
      elapsed = time.time() - self._start
      if self._process.returncode != 0:
        self._state = _FAILURE
        self._tempfile.seek(0)
        stdout = self._tempfile.read()
        message('FAILED', '%s [ret=%d]' % (
            self._spec.shortname, self._process.returncode), stdout)
      else:
        self._state = _SUCCESS
        message('PASSED', '%s [time=%.1fsec]' % (self._spec.shortname, elapsed),
                do_newline=self._newline_on_success or self._travis)
        if self._bin_hash:
          update_cache.finished(self._spec.identity(), self._bin_hash)
    elif self._state == _RUNNING and time.time() - self._start > 300:
      message('TIMEOUT', self._spec.shortname, do_newline=self._travis)
      self.kill()
    return self._state

  def kill(self):
    if self._state == _RUNNING:
      self._state = _KILLED
      self._process.terminate()


class Jobset(object):
  """Manages one run of jobs."""

  def __init__(self, check_cancelled, maxjobs, newline_on_success, travis, cache):
    self._running = set()
    self._check_cancelled = check_cancelled
    self._cancelled = False
    self._failures = 0
    self._completed = 0
    self._maxjobs = maxjobs
    self._newline_on_success = newline_on_success
    self._travis = travis
    self._cache = cache

  def start(self, spec):
    """Start a job. Return True on success, False on failure."""
    while len(self._running) >= self._maxjobs:
      if self.cancelled(): return False
      self.reap()
    if self.cancelled(): return False
    if spec.hash_targets:
      bin_hash = hashlib.sha1()
      for fn in spec.hash_targets:
        with open(which(fn)) as f:
          bin_hash.update(f.read())
      bin_hash = bin_hash.hexdigest()
      should_run = self._cache.should_run(spec.identity(), bin_hash)
    else:
      bin_hash = None
      should_run = True
    if should_run:
      self._running.add(Job(spec,
                            bin_hash,
                            self._newline_on_success,
                            self._travis))
    return True

  def reap(self):
    """Collect the dead jobs."""
    while self._running:
      dead = set()
      for job in self._running:
        st = job.state(self._cache)
        if st == _RUNNING: continue
        if st == _FAILURE: self._failures += 1
        if st == _KILLED: self._failures += 1
        dead.add(job)
      for job in dead:
        self._completed += 1
        self._running.remove(job)
      if dead: return
      if (not self._travis):
        message('WAITING', '%d jobs running, %d complete, %d failed' % (
            len(self._running), self._completed, self._failures))
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
        cache=None):
  js = Jobset(check_cancelled,
              maxjobs if maxjobs is not None else _DEFAULT_MAX_JOBS,
              newline_on_success, travis,
              cache if cache is not None else NoCache())
  if not travis:
    cmdlines = shuffle_iteratable(cmdlines)
  else:
    cmdlines = sorted(cmdlines, key=lambda x: x.shortname)
  for cmdline in cmdlines:
    if not js.start(cmdline):
      break
  return js.finish()
