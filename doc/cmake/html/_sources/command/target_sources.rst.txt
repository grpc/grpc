target_sources
--------------

.. versionadded:: 3.1

Add sources to a target.

.. code-block:: cmake

  target_sources(<target>
    <INTERFACE|PUBLIC|PRIVATE> [items1...]
    [<INTERFACE|PUBLIC|PRIVATE> [items2...] ...])

Specifies sources to use when building a target and/or its dependents.
Relative source file paths are interpreted as being relative to the current
source directory (i.e. :variable:`CMAKE_CURRENT_SOURCE_DIR`).  The
named ``<target>`` must have been created by a command such as
:command:`add_executable` or :command:`add_library` and must not be an
:ref:`ALIAS target <Alias Targets>`.

The ``INTERFACE``, ``PUBLIC`` and ``PRIVATE`` keywords are required to
specify the scope of the items following them.  ``PRIVATE`` and ``PUBLIC``
items will populate the :prop_tgt:`SOURCES` property of
``<target>``, which are used when building the target itself.
``PUBLIC`` and ``INTERFACE`` items will populate the
:prop_tgt:`INTERFACE_SOURCES` property of ``<target>``, which are used
when building dependents.  (:ref:`IMPORTED targets <Imported Targets>`
only support ``INTERFACE`` items because they are not build targets.)
The following arguments specify sources.  Repeated calls for the same
``<target>`` append items in the order called.

Arguments to ``target_sources`` may use "generator expressions"
with the syntax ``$<...>``. See the :manual:`cmake-generator-expressions(7)`
manual for available expressions.  See the :manual:`cmake-buildsystem(7)`
manual for more on defining buildsystem properties.

See also the :policy:`CMP0076` policy for older behavior related to the
handling of relative source file paths.
