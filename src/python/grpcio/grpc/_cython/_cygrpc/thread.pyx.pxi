# Copyright 2020 The gRPC authors.
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

def _contextvars_supported():
    """Determines if the contextvars module is supported.

    We use a 'try it and see if it works approach' here rather than predicting
    based on interpreter version in order to support older interpreters that
    may have a backported module based on, e.g. `threading.local`.

    Returns:
      A bool indicating whether `contextvars` are supported in the current
      environment.
    """
    try:
        import contextvars
        return True
    except ImportError:
        return False


def _run_with_context(target):
    """Runs a callable with contextvars propagated.

    If contextvars are supported, the calling thread's context will be copied
    and propagated. If they are not supported, this function is equivalent
    to the identity function.

    Args:
      target: A callable object to wrap.
    Returns:
      A callable object with the same signature as `target` but with
        contextvars propagated.
    """


if _contextvars_supported():
    import contextvars
    def _run_with_context(target):
        ctx = contextvars.copy_context()
        def _run(*args):
            ctx.run(target, *args)
        return _run
else:
    def _run_with_context(target):
        def _run(*args):
            target(*args)
        return _run
