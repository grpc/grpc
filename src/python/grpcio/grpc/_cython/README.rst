GRPC Python Cython layer
========================

Package for the GRPC Python Cython layer.

What is Cython?
---------------

Cython is both a superset of the Python language with extensions for dealing
with C types and a tool that transpiles this superset into C code. It provides
convenient means of statically typing expressions and of converting Python
strings to pointers (among other niceties), thus dramatically smoothing the
Python/C interop by allowing fluid use of APIs in both from the same source.
See the wonderful `Cython website`_.

Why Cython?
-----------

- **Python 2 and 3 support**
  Cython generated C code has precompiler macros to target both Python 2 and
  Python 3 C APIs, even while acting as a superset of just the Python 2
  language (e.g. using ``basestring``).
- **Significantly less semantic noise**
  A lot of CPython code is just glue, especially human-error-prone
  ``Py_INCREF``-ing and ``Py_DECREF``-ing around error handlers and such.
  Cython takes care of that automagically.
- **Possible PyPy support**
  One of the major developments in Cython over the past few years was the
  addition of support for PyPy. We might soon be able to provide such support
  ourselves through our use of Cython.
- **Less Python glue code**
  There existed several adapter layers in and around the original CPython code
  to smooth the surface exposed to Python due to how much trouble it was to
  make such a smooth surface via the CPython API alone. Cython makes writing
  such a surface incredibly easy, so these adapter layers may be removed.

Implications for Users
----------------------

Nothing additional will be required for users. PyPI packages will contain
Cython generated C code and thus not necessitate a Cython installation.

Implications for GRPC Developers
--------------------------------

A typical edit-compile-debug cycle now requires Cython. We install Cython in
the ``virtualenv`` generated for the Python tests in this repository, so
initial test runs may take an extra 2+ minutes to complete.  Subsequent test
runs won't reinstall ``Cython`` (unless required versions change and the
``virtualenv`` doesn't have installed versions that satisfy the change).

.. _`Cython website`: http://cython.org/
