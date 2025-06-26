"""Utilities to deal with running subprocesses."""

import subprocess


class BinaryRun:
  """Convenience wrapper around subprocess.

  Use as:
    ret, out, err = BinaryRun([exe, arg, ...]).communicate()
  """

  def __init__(self, args, dry_run=False):
    self.args = args
    self.results = None

    if dry_run:
      self.results = (0, b"", b"")
    else:
      self.proc = subprocess.Popen(  # pylint: disable=consider-using-with
          self.args, stdout=subprocess.PIPE, stderr=subprocess.PIPE
      )

  def communicate(self):
    if self.results:
      # We are running in dry-run mode.
      return self.results

    stdout, stderr = self.proc.communicate()
    self.results = self.proc.returncode, stdout, stderr
    return self.results


def can_run(exe, *args):
  """Check if running exe with args works."""
  try:
    BinaryRun([exe] + list(args)).communicate()
    return True
  except OSError:
    return False
