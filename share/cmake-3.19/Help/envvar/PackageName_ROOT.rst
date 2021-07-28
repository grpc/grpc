<PackageName>_ROOT
------------------

.. versionadded:: 3.12

.. include:: ENV_VAR.txt

Calls to :command:`find_package(<PackageName>)` will search in prefixes
specified by the ``<PackageName>_ROOT`` environment variable, where
``<PackageName>`` is the name given to the :command:`find_package` call
and ``_ROOT`` is literal.  For example, ``find_package(Foo)`` will search
prefixes specified in the ``Foo_ROOT`` environment variable (if set).
See policy :policy:`CMP0074`.

This variable may hold a single prefix or a list of prefixes separated
by ``:`` on UNIX or ``;`` on Windows (the same as the ``PATH`` environment
variable convention on those platforms).

See also the :variable:`<PackageName>_ROOT` CMake variable.
