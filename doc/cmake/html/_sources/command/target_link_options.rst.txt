target_link_options
-------------------

.. versionadded:: 3.13

Add options to the link step for an executable, shared library or module
library target.

.. code-block:: cmake

  target_link_options(<target> [BEFORE]
    <INTERFACE|PUBLIC|PRIVATE> [items1...]
    [<INTERFACE|PUBLIC|PRIVATE> [items2...] ...])

The named ``<target>`` must have been created by a command such as
:command:`add_executable` or :command:`add_library` and must not be an
:ref:`ALIAS target <Alias Targets>`.

This command can be used to add any link options, but alternative commands
exist to add libraries (:command:`target_link_libraries` or
:command:`link_libraries`).  See documentation of the
:prop_dir:`directory <LINK_OPTIONS>` and
:prop_tgt:`target <LINK_OPTIONS>` ``LINK_OPTIONS`` properties.

.. note::

  This command cannot be used to add options for static library targets,
  since they do not use a linker.  To add archiver or MSVC librarian flags,
  see the :prop_tgt:`STATIC_LIBRARY_OPTIONS` target property.

If ``BEFORE`` is specified, the content will be prepended to the property
instead of being appended.

The ``INTERFACE``, ``PUBLIC`` and ``PRIVATE`` keywords are required to
specify the scope of the following arguments.  ``PRIVATE`` and ``PUBLIC``
items will populate the :prop_tgt:`LINK_OPTIONS` property of
``<target>``.  ``PUBLIC`` and ``INTERFACE`` items will populate the
:prop_tgt:`INTERFACE_LINK_OPTIONS` property of ``<target>``.
(:ref:`IMPORTED targets <Imported Targets>` only support ``INTERFACE`` items.)
The following arguments specify link options.  Repeated calls for the same
``<target>`` append items in the order called.

Arguments to ``target_link_options`` may use "generator expressions"
with the syntax ``$<...>``. See the :manual:`cmake-generator-expressions(7)`
manual for available expressions.  See the :manual:`cmake-buildsystem(7)`
manual for more on defining buildsystem properties.

.. include:: DEVICE_LINK_OPTIONS.txt

.. include:: OPTIONS_SHELL.txt

.. include:: LINK_OPTIONS_LINKER.txt
