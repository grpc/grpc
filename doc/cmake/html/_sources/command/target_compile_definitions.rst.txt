target_compile_definitions
--------------------------

Add compile definitions to a target.

.. code-block:: cmake

  target_compile_definitions(<target>
    <INTERFACE|PUBLIC|PRIVATE> [items1...]
    [<INTERFACE|PUBLIC|PRIVATE> [items2...] ...])

Specifies compile definitions to use when compiling a given ``<target>``.  The
named ``<target>`` must have been created by a command such as
:command:`add_executable` or :command:`add_library` and must not be an
:ref:`ALIAS target <Alias Targets>`.

The ``INTERFACE``, ``PUBLIC`` and ``PRIVATE`` keywords are required to
specify the scope of the following arguments.  ``PRIVATE`` and ``PUBLIC``
items will populate the :prop_tgt:`COMPILE_DEFINITIONS` property of
``<target>``. ``PUBLIC`` and ``INTERFACE`` items will populate the
:prop_tgt:`INTERFACE_COMPILE_DEFINITIONS` property of ``<target>``.
(:ref:`IMPORTED targets <Imported Targets>` only support ``INTERFACE`` items.)
The following arguments specify compile definitions.  Repeated calls for the
same ``<target>`` append items in the order called.

Arguments to ``target_compile_definitions`` may use "generator expressions"
with the syntax ``$<...>``.  See the :manual:`cmake-generator-expressions(7)`
manual for available expressions.  See the :manual:`cmake-buildsystem(7)`
manual for more on defining buildsystem properties.

Any leading ``-D`` on an item will be removed.  Empty items are ignored.
For example, the following are all equivalent:

.. code-block:: cmake

  target_compile_definitions(foo PUBLIC FOO)
  target_compile_definitions(foo PUBLIC -DFOO)  # -D removed
  target_compile_definitions(foo PUBLIC "" FOO) # "" ignored
  target_compile_definitions(foo PUBLIC -D FOO) # -D becomes "", then ignored
