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

from __future__ import absolute_import

import collections
import multiprocessing
import os
import select
import signal
import sys
import tempfile
import threading
import time
import unittest
import uuid

import six
from six import moves

from tests import _loader
from tests import _result


class CaptureFile(object):
    """A context-managed file to redirect output to a byte array.

  Use by invoking `start` (`__enter__`) and at some point invoking `stop`
  (`__exit__`). At any point after the initial call to `start` call `output` to
  get the current redirected output. Note that we don't currently use file
  locking, so calling `output` between calls to `start` and `stop` may muddle
  the result (you should only be doing this during a Python-handled interrupt as
  a last ditch effort to provide output to the user).

  Attributes:
    _redirected_fd (int): File descriptor of file to redirect writes from.
    _saved_fd (int): A copy of the original value of the redirected file
      descriptor.
    _into_file (TemporaryFile or None): File to which writes are redirected.
      Only non-None when self is started.
  """

    def __init__(self, fd):
        self._redirected_fd = fd
        self._saved_fd = os.dup(self._redirected_fd)
        self._into_file = None

    def output(self):
        """Get all output from the redirected-to file if it exists."""
        if self._into_file:
            self._into_file.seek(0)
            return bytes(self._into_file.read())
        else:
            return bytes()

    def start(self):
        """Start redirection of writes to the file descriptor."""
        self._into_file = tempfile.TemporaryFile()
        os.dup2(self._into_file.fileno(), self._redirected_fd)

    def stop(self):
        """Stop redirection of writes to the file descriptor."""
        # n.b. this dup2 call auto-closes self._redirected_fd
        os.dup2(self._saved_fd, self._redirected_fd)

    def write_bypass(self, value):
        """Bypass the redirection and write directly to the original file.

    Arguments:
      value (str): What to write to the original file.
    """
        if six.PY3 and not isinstance(value, six.binary_type):
            value = bytes(value, 'ascii')
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


class AugmentedCase(collections.namedtuple('AugmentedCase', ['case', 'id'])):
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
        # only run test cases with id starting with given prefix
        testcase_filter = os.getenv('GRPC_PYTHON_TESTRUNNER_FILTER')
        filtered_cases = []
        for case in _loader.iterate_suite_cases(suite):
            if not testcase_filter or case.id().startswith(testcase_filter):
                filtered_cases.append(case)

        # Ensure that every test case has no collision with any other test case in
        # the augmented results.
        augmented_cases = [
            AugmentedCase(case, uuid.uuid4()) for case in filtered_cases
        ]
        case_id_by_case = dict((augmented_case.case, augmented_case.id)
                               for augmented_case in augmented_cases)
        result_out = moves.cStringIO()
        result = _result.TerminalResult(
            result_out, id_map=lambda case: case_id_by_case[case])
        stdout_pipe = CaptureFile(sys.stdout.fileno())
        stderr_pipe = CaptureFile(sys.stderr.fileno())
        kill_flag = [False]

        def sigint_handler(signal_number, frame):
            if signal_number == signal.SIGINT:
                kill_flag[0] = True  # Python 2.7 not having 'local'... :-(
            signal.signal(signal_number, signal.SIG_DFL)

        def fault_handler(signal_number, frame):
            stdout_pipe.write_bypass(
                'Received fault signal {}\nstdout:\n{}\n\nstderr:{}\n'.format(
                    signal_number, stdout_pipe.output(), stderr_pipe.output()))
            os._exit(1)

        def check_kill_self():
            if kill_flag[0]:
                stdout_pipe.write_bypass('Stopping tests short...')
                result.stopTestRun()
                stdout_pipe.write_bypass(result_out.getvalue())
                stdout_pipe.write_bypass('\ninterrupted stdout:\n{}\n'.format(
                    stdout_pipe.output().decode()))
                stderr_pipe.write_bypass('\ninterrupted stderr:\n{}\n'.format(
                    stderr_pipe.output().decode()))
                os._exit(1)

        def try_set_handler(name, handler):
            try:
                signal.signal(getattr(signal, name), handler)
            except AttributeError:
                pass

        try_set_handler('SIGINT', sigint_handler)
        try_set_handler('SIGSEGV', fault_handler)
        try_set_handler('SIGBUS', fault_handler)
        try_set_handler('SIGABRT', fault_handler)
        try_set_handler('SIGFPE', fault_handler)
        try_set_handler('SIGILL', fault_handler)
        # Sometimes output will lag after a test has successfully finished; we
        # ignore such writes to our pipes.
        try_set_handler('SIGPIPE', signal.SIG_IGN)

        # Run the tests
        result.startTestRun()
        for augmented_case in augmented_cases:
            sys.stdout.write(
                'Running       {}\n'.format(augmented_case.case.id()))
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
            result.set_output(augmented_case.case,
                              stdout_pipe.output(), stderr_pipe.output())
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
        with open('report.xml', 'wb') as report_xml_file:
            _result.jenkins_junit_xml(result).write(report_xml_file)
        return result
