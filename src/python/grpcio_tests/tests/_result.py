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

from __future__ import absolute_import

import collections
import io
import itertools
import traceback
import unittest
from xml.etree import ElementTree

import coverage

from tests import _loader


class CaseResult(
    collections.namedtuple(
        "CaseResult",
        ["id", "name", "kind", "stdout", "stderr", "skip_reason", "traceback"],
    )
):
    """A serializable result of a single test case.

    Attributes:
      id (object): Any serializable object used to denote the identity of this
        test case.
      name (str or None): A human-readable name of the test case.
      kind (CaseResult.Kind): The kind of test result.
      stdout (object or None): Output on stdout, or None if nothing was captured.
      stderr (object or None): Output on stderr, or None if nothing was captured.
      skip_reason (object or None): The reason the test was skipped. Must be
        something if self.kind is CaseResult.Kind.SKIP, else None.
      traceback (object or None): The traceback of the test. Must be something if
        self.kind is CaseResult.Kind.{ERROR, FAILURE, EXPECTED_FAILURE}, else
        None.
    """

    class Kind(object):
        UNTESTED = "untested"
        RUNNING = "running"
        ERROR = "error"
        FAILURE = "failure"
        SUCCESS = "success"
        SKIP = "skip"
        EXPECTED_FAILURE = "expected failure"
        UNEXPECTED_SUCCESS = "unexpected success"

    def __new__(
        cls,
        id=None,
        name=None,
        kind=None,
        stdout=None,
        stderr=None,
        skip_reason=None,
        traceback=None,
    ):
        """Helper keyword constructor for the namedtuple.

        See this class' attributes for information on the arguments."""
        assert id is not None
        assert name is None or isinstance(name, str)
        if kind is CaseResult.Kind.UNTESTED:
            pass
        elif kind is CaseResult.Kind.RUNNING:
            pass
        elif kind is CaseResult.Kind.ERROR:
            assert traceback is not None
        elif kind is CaseResult.Kind.FAILURE:
            assert traceback is not None
        elif kind is CaseResult.Kind.SUCCESS:
            pass
        elif kind is CaseResult.Kind.SKIP:
            assert skip_reason is not None
        elif kind is CaseResult.Kind.EXPECTED_FAILURE:
            assert traceback is not None
        elif kind is CaseResult.Kind.UNEXPECTED_SUCCESS:
            pass
        else:
            assert False
        return super(cls, CaseResult).__new__(
            cls, id, name, kind, stdout, stderr, skip_reason, traceback
        )

    def updated(
        self,
        name=None,
        kind=None,
        stdout=None,
        stderr=None,
        skip_reason=None,
        traceback=None,
    ):
        """Get a new validated CaseResult with the fields updated.

        See this class' attributes for information on the arguments."""
        name = self.name if name is None else name
        kind = self.kind if kind is None else kind
        stdout = self.stdout if stdout is None else stdout
        stderr = self.stderr if stderr is None else stderr
        skip_reason = self.skip_reason if skip_reason is None else skip_reason
        traceback = self.traceback if traceback is None else traceback
        return CaseResult(
            id=self.id,
            name=name,
            kind=kind,
            stdout=stdout,
            stderr=stderr,
            skip_reason=skip_reason,
            traceback=traceback,
        )


class AugmentedResult(unittest.TestResult):
    """unittest.Result that keeps track of additional information.

    Uses CaseResult objects to store test-case results, providing additional
    information beyond that of the standard Python unittest library, such as
    standard output.

    Attributes:
      id_map (callable): A unary callable mapping unittest.TestCase objects to
        unique identifiers.
      cases (dict): A dictionary mapping from the identifiers returned by id_map
        to CaseResult objects corresponding to those IDs.
    """

    def __init__(self, id_map):
        """Initialize the object with an identifier mapping.

        Arguments:
          id_map (callable): Corresponds to the attribute `id_map`."""
        super(AugmentedResult, self).__init__()
        self.id_map = id_map
        self.cases = None

    def startTestRun(self):
        """See unittest.TestResult.startTestRun."""
        super(AugmentedResult, self).startTestRun()
        self.cases = dict()

    def startTest(self, test):
        """See unittest.TestResult.startTest."""
        super(AugmentedResult, self).startTest(test)
        case_id = self.id_map(test)
        self.cases[case_id] = CaseResult(
            id=case_id, name=test.id(), kind=CaseResult.Kind.RUNNING
        )

    def addError(self, test, err):
        """See unittest.TestResult.addError."""
        super(AugmentedResult, self).addError(test, err)
        case_id = self.id_map(test)
        self.cases[case_id] = self.cases[case_id].updated(
            kind=CaseResult.Kind.ERROR, traceback=err
        )

    def addFailure(self, test, err):
        """See unittest.TestResult.addFailure."""
        super(AugmentedResult, self).addFailure(test, err)
        case_id = self.id_map(test)
        self.cases[case_id] = self.cases[case_id].updated(
            kind=CaseResult.Kind.FAILURE, traceback=err
        )

    def addSuccess(self, test):
        """See unittest.TestResult.addSuccess."""
        super(AugmentedResult, self).addSuccess(test)
        case_id = self.id_map(test)
        self.cases[case_id] = self.cases[case_id].updated(
            kind=CaseResult.Kind.SUCCESS
        )

    def addSkip(self, test, reason):
        """See unittest.TestResult.addSkip."""
        super(AugmentedResult, self).addSkip(test, reason)
        case_id = self.id_map(test)
        self.cases[case_id] = self.cases[case_id].updated(
            kind=CaseResult.Kind.SKIP, skip_reason=reason
        )

    def addExpectedFailure(self, test, err):
        """See unittest.TestResult.addExpectedFailure."""
        super(AugmentedResult, self).addExpectedFailure(test, err)
        case_id = self.id_map(test)
        self.cases[case_id] = self.cases[case_id].updated(
            kind=CaseResult.Kind.EXPECTED_FAILURE, traceback=err
        )

    def addUnexpectedSuccess(self, test):
        """See unittest.TestResult.addUnexpectedSuccess."""
        super(AugmentedResult, self).addUnexpectedSuccess(test)
        case_id = self.id_map(test)
        self.cases[case_id] = self.cases[case_id].updated(
            kind=CaseResult.Kind.UNEXPECTED_SUCCESS
        )

    def set_output(self, test, stdout, stderr):
        """Set the output attributes for the CaseResult corresponding to a test.

        Args:
          test (unittest.TestCase): The TestCase to set the outputs of.
          stdout (str): Output from stdout to assign to self.id_map(test).
          stderr (str): Output from stderr to assign to self.id_map(test).
        """
        case_id = self.id_map(test)
        self.cases[case_id] = self.cases[case_id].updated(
            stdout=stdout.decode(), stderr=stderr.decode()
        )

    def augmented_results(self, filter):
        """Convenience method to retrieve filtered case results.

        Args:
          filter (callable): A unary predicate to filter over CaseResult objects.
        """
        return (
            self.cases[case_id]
            for case_id in self.cases
            if filter(self.cases[case_id])
        )


class CoverageResult(AugmentedResult):
    """Extension to AugmentedResult adding coverage.py support per test.\

  Attributes:
    coverage_context (coverage.Coverage): coverage.py management object.
  """

    def __init__(self, id_map):
        """See AugmentedResult.__init__."""
        super(CoverageResult, self).__init__(id_map=id_map)
        self.coverage_context = None

    def startTest(self, test):
        """See unittest.TestResult.startTest.

        Additionally initializes and begins code coverage tracking."""
        super(CoverageResult, self).startTest(test)
        self.coverage_context = coverage.Coverage(data_suffix=True)
        self.coverage_context.start()

    def stopTest(self, test):
        """See unittest.TestResult.stopTest.

        Additionally stops and deinitializes code coverage tracking."""
        super(CoverageResult, self).stopTest(test)
        self.coverage_context.stop()
        self.coverage_context.save()
        self.coverage_context = None


class _Colors(object):
    """Namespaced constants for terminal color magic numbers."""

    HEADER = "\033[95m"
    INFO = "\033[94m"
    OK = "\033[92m"
    WARN = "\033[93m"
    FAIL = "\033[91m"
    BOLD = "\033[1m"
    UNDERLINE = "\033[4m"
    END = "\033[0m"


class TerminalResult(CoverageResult):
    """Extension to CoverageResult adding basic terminal reporting."""

    def __init__(self, out, id_map):
        """Initialize the result object.

        Args:
          out (file-like): Output file to which terminal-colored live results will
            be written.
          id_map (callable): See AugmentedResult.__init__.
        """
        super(TerminalResult, self).__init__(id_map=id_map)
        self.out = out

    def startTestRun(self):
        """See unittest.TestResult.startTestRun."""
        super(TerminalResult, self).startTestRun()
        self.out.write(
            _Colors.HEADER + "Testing gRPC Python...\n" + _Colors.END
        )

    def stopTestRun(self):
        """See unittest.TestResult.stopTestRun."""
        super(TerminalResult, self).stopTestRun()
        self.out.write(summary(self))
        self.out.flush()

    def addError(self, test, err):
        """See unittest.TestResult.addError."""
        super(TerminalResult, self).addError(test, err)
        self.out.write(
            _Colors.FAIL + f"ERROR         {test.id()}\n" + _Colors.END
        )
        self.out.flush()

    def addFailure(self, test, err):
        """See unittest.TestResult.addFailure."""
        super(TerminalResult, self).addFailure(test, err)
        self.out.write(
            _Colors.FAIL + f"FAILURE       {test.id()}\n" + _Colors.END
        )
        self.out.flush()

    def addSuccess(self, test):
        """See unittest.TestResult.addSuccess."""
        super(TerminalResult, self).addSuccess(test)
        self.out.write(
            _Colors.OK + f"SUCCESS       {test.id()}\n" + _Colors.END
        )
        self.out.flush()

    def addSkip(self, test, reason):
        """See unittest.TestResult.addSkip."""
        super(TerminalResult, self).addSkip(test, reason)
        self.out.write(
            _Colors.INFO + f"SKIP          {test.id()}\n" + _Colors.END
        )
        self.out.flush()

    def addExpectedFailure(self, test, err):
        """See unittest.TestResult.addExpectedFailure."""
        super(TerminalResult, self).addExpectedFailure(test, err)
        self.out.write(
            _Colors.INFO + f"FAILURE_OK    {test.id()}\n" + _Colors.END
        )
        self.out.flush()

    def addUnexpectedSuccess(self, test):
        """See unittest.TestResult.addUnexpectedSuccess."""
        super(TerminalResult, self).addUnexpectedSuccess(test)
        self.out.write(
            _Colors.INFO + f"UNEXPECTED_OK {test.id()}\n" + _Colors.END
        )
        self.out.flush()


def _traceback_string(type, value, trace):
    """Generate a descriptive string of a Python exception traceback.

    Args:
      type (class): The type of the exception.
      value (Exception): The value of the exception.
      trace (traceback): Traceback of the exception.

    Returns:
      str: Formatted exception descriptive string.
    """
    buffer = io.StringIO()
    traceback.print_exception(type, value, trace, file=buffer)
    return buffer.getvalue()


def summary(result):
    """A summary string of a result object.

    Args:
      result (AugmentedResult): The result object to get the summary of.

    Returns:
      str: The summary string.
    """
    assert isinstance(result, AugmentedResult)
    untested = list(
        result.augmented_results(
            lambda case_result: case_result.kind is CaseResult.Kind.UNTESTED
        )
    )
    running = list(
        result.augmented_results(
            lambda case_result: case_result.kind is CaseResult.Kind.RUNNING
        )
    )
    failures = list(
        result.augmented_results(
            lambda case_result: case_result.kind is CaseResult.Kind.FAILURE
        )
    )
    errors = list(
        result.augmented_results(
            lambda case_result: case_result.kind is CaseResult.Kind.ERROR
        )
    )
    successes = list(
        result.augmented_results(
            lambda case_result: case_result.kind is CaseResult.Kind.SUCCESS
        )
    )
    skips = list(
        result.augmented_results(
            lambda case_result: case_result.kind is CaseResult.Kind.SKIP
        )
    )
    expected_failures = list(
        result.augmented_results(
            lambda case_result: case_result.kind
            is CaseResult.Kind.EXPECTED_FAILURE
        )
    )
    unexpected_successes = list(
        result.augmented_results(
            lambda case_result: case_result.kind
            is CaseResult.Kind.UNEXPECTED_SUCCESS
        )
    )
    running_names = [case.name for case in running]
    finished_count = (
        len(failures)
        + len(errors)
        + len(successes)
        + len(expected_failures)
        + len(unexpected_successes)
    )
    statistics = (
        "{finished} tests finished:\n"
        "\t{successful} successful\n"
        "\t{unsuccessful} unsuccessful\n"
        "\t{skipped} skipped\n"
        "\t{expected_fail} expected failures\n"
        "\t{unexpected_successful} unexpected successes\n"
        "Interrupted Tests:\n"
        "\t{interrupted}\n".format(
            finished=finished_count,
            successful=len(successes),
            unsuccessful=(len(failures) + len(errors)),
            skipped=len(skips),
            expected_fail=len(expected_failures),
            unexpected_successful=len(unexpected_successes),
            interrupted=str(running_names),
        )
    )
    tracebacks = "\n\n".join(
        [
            (
                _Colors.FAIL
                + "{test_name}"
                + _Colors.END
                + "\n"
                + _Colors.BOLD
                + "traceback:"
                + _Colors.END
                + "\n"
                + "{traceback}\n"
                + _Colors.BOLD
                + "stdout:"
                + _Colors.END
                + "\n"
                + "{stdout}\n"
                + _Colors.BOLD
                + "stderr:"
                + _Colors.END
                + "\n"
                + "{stderr}\n"
            ).format(
                test_name=result.name,
                traceback=_traceback_string(*result.traceback),
                stdout=result.stdout,
                stderr=result.stderr,
            )
            for result in itertools.chain(failures, errors)
        ]
    )
    notes = "Unexpected successes: {}\n".format(
        [result.name for result in unexpected_successes]
    )
    return statistics + "\nErrors/Failures: \n" + tracebacks + "\n" + notes


def jenkins_junit_xml(result):
    """An XML tree object that when written is recognizable by Jenkins.

    Args:
      result (AugmentedResult): The result object to get the junit xml output of.

    Returns:
      ElementTree.ElementTree: The XML tree.
    """
    assert isinstance(result, AugmentedResult)
    root = ElementTree.Element("testsuites")
    suite = ElementTree.SubElement(
        root,
        "testsuite",
        {
            "name": "Python gRPC tests",
        },
    )
    for case in result.cases.values():
        if case.kind is CaseResult.Kind.SUCCESS:
            ElementTree.SubElement(
                suite,
                "testcase",
                {
                    "name": case.name,
                },
            )
        elif case.kind in (CaseResult.Kind.ERROR, CaseResult.Kind.FAILURE):
            case_xml = ElementTree.SubElement(
                suite,
                "testcase",
                {
                    "name": case.name,
                },
            )
            error_xml = ElementTree.SubElement(case_xml, "error", {})
            error_xml.text = "".format(case.stderr, case.traceback)
    return ElementTree.ElementTree(element=root)
