add_executable
--------------

.. only:: html

  .. contents::

Add an executable to the project using the specified source files.

Normal Executables
^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

  add_executable(<name> [WIN32] [MACOSX_BUNDLE]
                 [EXCLUDE_FROM_ALL]
                 [source1] [source2 ...])

Adds an executable target called ``<name>`` to be built from the source
files listed in the command invocation.  (The source files can be omitted
here if they are added later using :command:`target_sources`.)  The
``<name>`` corresponds to the logical target name and must be globally
unique within a project.  The actual file name of the executable built is
constructed based on conventions of the native platform (such as
``<name>.exe`` or just ``<name>``).

By default the executable file will be created in the build tree
directory corresponding to the source tree directory in which the
command was invoked.  See documentation of the
:prop_tgt:`RUNTIME_OUTPUT_DIRECTORY` target property to change this
location.  See documentation of the :prop_tgt:`OUTPUT_NAME` target property
to change the ``<name>`` part of the final file name.

If ``WIN32`` is given the property :prop_tgt:`WIN32_EXECUTABLE` will be
set on the target created.  See documentation of that target property for
details.

If ``MACOSX_BUNDLE`` is given the corresponding property will be set on
the created target.  See documentation of the :prop_tgt:`MACOSX_BUNDLE`
target property for details.

If ``EXCLUDE_FROM_ALL`` is given the corresponding property will be set on
the created target.  See documentation of the :prop_tgt:`EXCLUDE_FROM_ALL`
target property for details.

Source arguments to ``add_executable`` may use "generator expressions" with
the syntax ``$<...>``.  See the :manual:`cmake-generator-expressions(7)`
manual for available expressions.  See the :manual:`cmake-buildsystem(7)`
manual for more on defining buildsystem properties.

See also :prop_sf:`HEADER_FILE_ONLY` on what to do if some sources are
pre-processed, and you want to have the original sources reachable from
within IDE.

Imported Executables
^^^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

  add_executable(<name> IMPORTED [GLOBAL])

An :ref:`IMPORTED executable target <Imported Targets>` references an
executable file located outside the project.  No rules are generated to
build it, and the :prop_tgt:`IMPORTED` target property is ``True``.  The
target name has scope in the directory in which it is created and below, but
the ``GLOBAL`` option extends visibility.  It may be referenced like any
target built within the project.  ``IMPORTED`` executables are useful
for convenient reference from commands like :command:`add_custom_command`.
Details about the imported executable are specified by setting properties
whose names begin in ``IMPORTED_``.  The most important such property is
:prop_tgt:`IMPORTED_LOCATION` (and its per-configuration version
:prop_tgt:`IMPORTED_LOCATION_<CONFIG>`) which specifies the location of
the main executable file on disk.  See documentation of the ``IMPORTED_*``
properties for more information.

Alias Executables
^^^^^^^^^^^^^^^^^

.. code-block:: cmake

  add_executable(<name> ALIAS <target>)

Creates an :ref:`Alias Target <Alias Targets>`, such that ``<name>`` can
be used to refer to ``<target>`` in subsequent commands.  The ``<name>``
does not appear in the generated buildsystem as a make target.  The
``<target>`` may not be an ``ALIAS``.

An ``ALIAS`` to a non-``GLOBAL`` :ref:`Imported Target <Imported Targets>`
has scope in the directory in which the alias is created and below.
The :prop_tgt:`ALIAS_GLOBAL` target property can be used to check if the
alias is global or not.

``ALIAS`` targets can be used as targets to read properties
from, executables for custom commands and custom targets.  They can also be
tested for existence with the regular :command:`if(TARGET)` subcommand.
The ``<name>`` may not be used to modify properties of ``<target>``, that
is, it may not be used as the operand of :command:`set_property`,
:command:`set_target_properties`, :command:`target_link_libraries` etc.
An ``ALIAS`` target may not be installed or exported.
