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

import cStringIO as StringIO
import collections
import fcntl
import multiprocessing
import os
import select
import signal
import sys
import threading
import time
import unittest
import uuid

from tests import _loader
from tests import _result


class CapturePipe(object):
  """A context-manager pipe to redirect output to a byte array.

  Attributes:
    _redirect_fd (int): File descriptor of file to redirect writes from.
    _saved_fd (int): A copy of the original value of the redirected file
      descriptor.
    _read_thread (threading.Thread or None): Thread upon which reads through the
      pipe are performed. Only non-None when self is started.
    _read_fd (int or None): File descriptor of the read end of the redirect
      pipe. Only non-None when self is started.
    _write_fd (int or None): File descriptor of the write end of the redirect
      pipe. Only non-None when self is started.
    output (bytearray or None): Redirected output from writes to the redirected
      file descriptor. Only valid during and after self has started.
  """

  def __init__(self, fd):
    self._redirect_fd = fd
    self._saved_fd = os.dup(self._redirect_fd)
    self._read_thread = None
    self._read_fd = None
    self._write_fd = None
    self.output = None

  def start(self):
    """Start redirection of writes to the file descriptor."""
    self._read_fd, self._write_fd = os.pipe()
    os.dup2(self._write_fd, self._redirect_fd)
    flags = fcntl.fcntl(self._read_fd, fcntl.F_GETFL)
    fcntl.fcntl(self._read_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)
    self._read_thread = threading.Thread(target=self._read)
    self._read_thread.start()

  def stop(self):
    """Stop redirection of writes to the file descriptor."""
    os.close(self._write_fd)
    os.dup2(self._saved_fd, self._redirect_fd)  # auto-close self._redirect_fd
    self._read_thread.join()
    self._read_thread = None
    # we waited for the read thread to finish, so _read_fd has been read and we
    # can close it.
    os.close(self._read_fd)

  def _read(self):
    """Read-thread target for self."""
    self.output = bytearray()
    while True:
      select.select([self._read_fd], [], [])
      read_bytes = os.read(self._read_fd, 1024)
      if read_bytes:
        self.output.extend(read_bytes)
      else:
        break

  def write_bypass(self, value):
    """Bypass the redirection and write directly to the original file.

    Arguments:
      value (str): What to write to the original file.
    """
    if self._saved_fd is None:
      os.write(self._redirect_fd, value)
    else:
      os.write(self._saved_fd, value)

  def __enter__(self):
    self.start()
    return self

  def __exit__(self, type, value, traceback):
    self.stop()

  def close(self):
    """Close any resources used by self not closed by stop()."""
    os.close(self._saved_fd)


class AugmentedCase(collections.namedtuple('AugmentedCase', [
    'case', 'id'])):
  """A test case with a guaranteed unique externally specified identifier.

  Attributes:
    case (unittest.TestCase): TestCase we're decorating with an additional
      identifier.
    id (object): Any identifier that may be considered 'unique' for testing
      purposes.
  """

  def __new__(cls, case, id=None):
    if id is None:
      id = uuid.uuid4()
    return super(cls, AugmentedCase).__new__(cls, case, id)


class Runner(object):

  def run(self, suite):
    """See setuptools' test_runner setup argument for information."""
    # Ensure that every test case has no collision with any other test case in
    # the augmented results.
    augmented_cases = [AugmentedCase(case, uuid.uuid4())
                       for case in _loader.iterate_suite_cases(suite)]
    case_id_by_case = dict((augmented_case.case, augmented_case.id)
                           for augmented_case in augmented_cases)
    result_out = StringIO.StringIO()
    result = _result.TerminalResult(
        result_out, id_map=lambda case: case_id_by_case[case])
    stdout_pipe = CapturePipe(sys.stdout.fileno())
    stderr_pipe = CapturePipe(sys.stderr.fileno())
    kill_flag = [False]

    def sigint_handler(signal_number, frame):
      if signal_number == signal.SIGINT:
        kill_flag[0] = True  # Python 2.7 not having 'local'... :-(
      signal.signal(signal_number, signal.SIG_DFL)

    def fault_handler(signal_number, frame):
      stdout_pipe.write_bypass(
          'Received fault signal {}\nstdout:\n{}\n\nstderr:{}\n'
          .format(signal_number, stdout_pipe.output, stderr_pipe.output))
      os._exit(1)

    def check_kill_self():
      if kill_flag[0]:
        stdout_pipe.write_bypass('Stopping tests short...')
        result.stopTestRun()
        stdout_pipe.write_bypass(result_out.getvalue())
        stdout_pipe.write_bypass(
            '\ninterrupted stdout:\n{}\n'.format(stdout_pipe.output))
        stderr_pipe.write_bypass(
            '\ninterrupted stderr:\n{}\n'.format(stderr_pipe.output))
        os._exit(1)
    signal.signal(signal.SIGINT, sigint_handler)
    signal.signal(signal.SIGSEGV, fault_handler)
    signal.signal(signal.SIGBUS, fault_handler)
    signal.signal(signal.SIGABRT, fault_handler)
    signal.signal(signal.SIGFPE, fault_handler)
    signal.signal(signal.SIGILL, fault_handler)
    # Sometimes output will lag after a test has successfully finished; we
    # ignore such writes to our pipes.
    signal.signal(signal.SIGPIPE, signal.SIG_IGN)

    # Run the tests
    result.startTestRun()
    for augmented_case in augmented_cases:
      sys.stdout.write('Running       {}\n'.format(augmented_case.case.id()))
      sys.stdout.flush()
      case_thread = threading.Thread(
          target=augmented_case.case.run, args=(result,))
      try:
        with stdout_pipe, stderr_pipe:
          case_thread.start()
          while case_thread.is_alive():
            check_kill_self()
            time.sleep(0)
          case_thread.join()
      except:
        # re-raise the exception after forcing the with-block to end
        raise
      result.set_output(
          augmented_case.case, stdout_pipe.output, stderr_pipe.output)
      sys.stdout.write(result_out.getvalue())
      sys.stdout.flush()
      result_out.truncate(0)
      check_kill_self()
    result.stopTestRun()
    stdout_pipe.close()
    stderr_pipe.close()

    # Report results
    sys.stdout.write(result_out.getvalue())
    sys.stdout.flush()
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    with open('report.xml', 'w') as report_xml_file:
      _result.jenkins_junit_xml(result).write(report_xml_file)
    return result

