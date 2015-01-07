"""Run a group of subprocesses and then finish."""

import multiprocessing
import random
import subprocess
import sys
import threading

# multiplicative factor to over subscribe CPU cores
# (many tests sleep for a long time)
_OVERSUBSCRIBE = 32
_active_jobs = threading.Semaphore(
    multiprocessing.cpu_count() * _OVERSUBSCRIBE)
_output_lock = threading.Lock()


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
      p *= 2
      yield val
    else:
      nextit.append(val)
  # after taking a random sampling, we shuffle the rest of the elements and
  # yield them
  random.shuffle(nextit)
  for val in nextit:
    yield val


class Jobset(object):
  """Manages one run of jobs."""

  def __init__(self, cmdlines):
    self._cmdlines = shuffle_iteratable(cmdlines)
    self._failures = 0

  def _run_thread(self, cmdline):
    try:
      # start the process
      p = subprocess.Popen(args=cmdline,
                           stderr=subprocess.STDOUT,
                           stdout=subprocess.PIPE)
      stdout, _ = p.communicate()
      # log output (under a lock)
      _output_lock.acquire()
      try:
        if p.returncode != 0:
          sys.stdout.write('\x1b[0G\x1b[2K\x1b[31mFAILED\x1b[0m: %s'
                           ' [ret=%d]\n'
                           '%s\n' % (
                               ' '.join(cmdline), p.returncode,
                               stdout))
          self._failures += 1
        else:
          sys.stdout.write('\x1b[0G\x1b[2K\x1b[32mPASSED\x1b[0m: %s' %
                           ' '.join(cmdline))
        sys.stdout.flush()
      finally:
        _output_lock.release()
    finally:
      _active_jobs.release()

  def run(self):
    threads = []
    for cmdline in self._cmdlines:
      # cap number of active jobs - release in _run_thread
      _active_jobs.acquire()
      t = threading.Thread(target=self._run_thread,
                           args=[cmdline])
      t.start()
      threads.append(t)
    for thread in threads:
      thread.join()
    return self._failures == 0


def run(cmdlines):
  return Jobset(cmdlines).run()

