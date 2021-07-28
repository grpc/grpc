target_link_libraries
---------------------

.. only:: html

   .. contents::

Specify libraries or flags to use when linking a given target and/or
its dependents.  :ref:`Usage requirements <Target Usage Requirements>`
from linked library targets will be propagated.  Usage requirements
of a target's dependencies affect compilation of its own sources.

Overview
^^^^^^^^

This command has several signatures as detailed in subsections below.
All of them have the general form

.. code-block:: cmake

  target_link_libraries(<target> ... <item>... ...)

The named ``<target>`` must have been created by a command such as
:command:`add_executable` or :command:`add_library` and must not be an
:ref:`ALIAS target <Alias Targets>`.  If policy :policy:`CMP0079` is not
set to ``NEW`` then the target must have been created in the current
directory.  Repeated calls for the same ``<target>`` append items in
the order called.

Each ``<item>`` may be:

* **A library target name**: The generated link line will have the
  full path to the linkable library file associated with the target.
  The buildsystem will have a dependency to re-link ``<target>`` if
  the library file changes.

  The named target must be created by :command:`add_library` within
  the project or as an :ref:`IMPORTED library <Imported Targets>`.
  If it is created within the project an ordering dependency will
  automatically be added in the build system to make sure the named
  library target is up-to-date before the ``<target>`` links.

  If an imported library has the :prop_tgt:`IMPORTED_NO_SONAME`
  target property set, CMake may ask the linker to search for
  the library instead of using the full path
  (e.g. ``/usr/lib/libfoo.so`` becomes ``-lfoo``).

  The full path to the target's artifact will be quoted/escaped for
  the shell automatically.

* **A full path to a library file**: The generated link line will
  normally preserve the full path to the file. The buildsystem will
  have a dependency to re-link ``<target>`` if the library file changes.

  There are some cases where CMake may ask the linker to search for
  the library (e.g. ``/usr/lib/libfoo.so`` becomes ``-lfoo``), such
  as when a shared library is detected to have no ``SONAME`` field.
  See policy :policy:`CMP0060` for discussion of another case.

  If the library file is in a macOS framework, the ``Headers`` directory
  of the framework will also be processed as a
  :ref:`usage requirement <Target Usage Requirements>`.  This has the same
  effect as passing the framework directory as an include directory.

  On :ref:`Visual Studio Generators` for VS 2010 and above, library files
  ending in ``.targets`` will be treated as MSBuild targets files and
  imported into generated project files.  This is not supported by other
  generators.

  The full path to the library file will be quoted/escaped for
  the shell automatically.

* **A plain library name**: The generated link line will ask the linker
  to search for the library (e.g. ``foo`` becomes ``-lfoo`` or ``foo.lib``).

  The library name/flag is treated as a command-line string fragment and
  will be used with no extra quoting or escaping.

* **A link flag**: Item names starting with ``-``, but not ``-l`` or
  ``-framework``, are treated as linker flags.  Note that such flags will
  be treated like any other library link item for purposes of transitive
  dependencies, so they are generally safe to specify only as private link
  items that will not propagate to dependents.

  Link flags specified here are inserted into the link command in the same
  place as the link libraries. This might not be correct, depending on
  the linker. Use the :prop_tgt:`LINK_OPTIONS` target property or
  :command:`target_link_options` command to add link
  flags explicitly. The flags will then be placed at the toolchain-defined
  flag position in the link command.

  The link flag is treated as a command-line string fragment and
  will be used with no extra quoting or escaping.

* **A generator expression**: A ``$<...>`` :manual:`generator expression
  <cmake-generator-expressions(7)>` may evaluate to any of the above
  items or to a :ref:`semicolon-separated list <CMake Language Lists>` of them.
  If the ``...`` contains any ``;`` characters, e.g. after evaluation
  of a ``${list}`` variable, be sure to use an explicitly quoted
  argument ``"$<...>"`` so that this command receives it as a
  single ``<item>``.

  Additionally, a generator expression may be used as a fragment of
  any of the above items, e.g. ``foo$<1:_d>``.

  Note that generator expressions will not be used in OLD handling of
  policy :policy:`CMP0003` or policy :policy:`CMP0004`.

* A ``debug``, ``optimized``, or ``general`` keyword immediately followed
  by another ``<item>``.  The item following such a keyword will be used
  only for the corresponding build configuration.  The ``debug`` keyword
  corresponds to the ``Debug`` configuration (or to configurations named
  in the :prop_gbl:`DEBUG_CONFIGURATIONS` global property if it is set).
  The ``optimized`` keyword corresponds to all other configurations.  The
  ``general`` keyword corresponds to all configurations, and is purely
  optional.  Higher granularity may be achieved for per-configuration
  rules by creating and linking to
  :ref:`IMPORTED library targets <Imported Targets>`.
  These keywords are interpreted immediately by this command and therefore
  have no special meaning when produced by a generator expression.

Items containing ``::``, such as ``Foo::Bar``, are assumed to be
:ref:`IMPORTED <Imported Targets>` or :ref:`ALIAS <Alias Targets>` library
target names and will cause an error if no such target exists.
See policy :policy:`CMP0028`.

See the :manual:`cmake-buildsystem(7)` manual for more on defining
buildsystem properties.

Libraries for a Target and/or its Dependents
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

  target_link_libraries(<target>
                        <PRIVATE|PUBLIC|INTERFACE> <item>...
                       [<PRIVATE|PUBLIC|INTERFACE> <item>...]...)

The ``PUBLIC``, ``PRIVATE`` and ``INTERFACE`` keywords can be used to
specify both the link dependencies and the link interface in one command.
Libraries and targets following ``PUBLIC`` are linked to, and are made
part of the link interface.  Libraries and targets following ``PRIVATE``
are linked to, but are not made part of the link interface.  Libraries
following ``INTERFACE`` are appended to the link interface and are not
used for linking ``<target>``.

Libraries for both a Target and its Dependents
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

  target_link_libraries(<target> <item>...)

Library dependencies are transitive by default with this signature.
When this target is linked into another target then the libraries
linked to this target will appear on the link line for the other
target too.  This transitive "link interface" is stored in the
:prop_tgt:`INTERFACE_LINK_LIBRARIES` target property and may be overridden
by setting the property directly.  When :policy:`CMP0022` is not set to
``NEW``, transitive linking is built in but may be overridden by the
:prop_tgt:`LINK_INTERFACE_LIBRARIES` property.  Calls to other signatures
of this command may set the property making any libraries linked
exclusively by this signature private.

Libraries for a Target and/or its Dependents (Legacy)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

  target_link_libraries(<target>
                        <LINK_PRIVATE|LINK_PUBLIC> <lib>...
                       [<LINK_PRIVATE|LINK_PUBLIC> <lib>...]...)

The ``LINK_PUBLIC`` and ``LINK_PRIVATE`` modes can be used to specify both
the link dependencies and the link interface in one command.

This signature is for compatibility only.  Prefer the ``PUBLIC`` or
``PRIVATE`` keywords instead.

Libraries and targets following ``LINK_PUBLIC`` are linked to, and are
made part of the :prop_tgt:`INTERFACE_LINK_LIBRARIES`.  If policy
:policy:`CMP0022` is not ``NEW``, they are also made part of the
:prop_tgt:`LINK_INTERFACE_LIBRARIES`.  Libraries and targets following
``LINK_PRIVATE`` are linked to, but are not made part of the
:prop_tgt:`INTERFACE_LINK_LIBRARIES` (or :prop_tgt:`LINK_INTERFACE_LIBRARIES`).

Libraries for Dependents Only (Legacy)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

  target_link_libraries(<target> LINK_INTERFACE_LIBRARIES <item>...)

The ``LINK_INTERFACE_LIBRARIES`` mode appends the libraries to the
:prop_tgt:`INTERFACE_LINK_LIBRARIES` target property instead of using them
for linking.  If policy :policy:`CMP0022` is not ``NEW``, then this mode
also appends libraries to the :prop_tgt:`LINK_INTERFACE_LIBRARIES` and its
per-configuration equivalent.

This signature is for compatibility only.  Prefer the ``INTERFACE`` mode
instead.

Libraries specified as ``debug`` are wrapped in a generator expression to
correspond to debug builds.  If policy :policy:`CMP0022` is
not ``NEW``, the libraries are also appended to the
:prop_tgt:`LINK_INTERFACE_LIBRARIES_DEBUG <LINK_INTERFACE_LIBRARIES_<CONFIG>>`
property (or to the properties corresponding to configurations listed in
the :prop_gbl:`DEBUG_CONFIGURATIONS` global property if it is set).
Libraries specified as ``optimized`` are appended to the
:prop_tgt:`INTERFACE_LINK_LIBRARIES` property.  If policy :policy:`CMP0022`
is not ``NEW``, they are also appended to the
:prop_tgt:`LINK_INTERFACE_LIBRARIES` property.  Libraries specified as
``general`` (or without any keyword) are treated as if specified for both
``debug`` and ``optimized``.

Linking Object Libraries
^^^^^^^^^^^^^^^^^^^^^^^^

:ref:`Object Libraries` may be used as the ``<target>`` (first) argument
of ``target_link_libraries`` to specify dependencies of their sources
on other libraries.  For example, the code

.. code-block:: cmake

  add_library(A SHARED a.c)
  target_compile_definitions(A PUBLIC A)

  add_library(obj OBJECT obj.c)
  target_compile_definitions(obj PUBLIC OBJ)
  target_link_libraries(obj PUBLIC A)

compiles ``obj.c`` with ``-DA -DOBJ`` and establishes usage requirements
for ``obj`` that propagate to its dependents.

Normal libraries and executables may link to :ref:`Object Libraries`
to get their objects and usage requirements.  Continuing the above
example, the code

.. code-block:: cmake

  add_library(B SHARED b.c)
  target_link_libraries(B PUBLIC obj)

compiles ``b.c`` with ``-DA -DOBJ``, creates shared library ``B``
with object files from ``b.c`` and ``obj.c``, and links ``B`` to ``A``.
Furthermore, the code

.. code-block:: cmake

  add_executable(main main.c)
  target_link_libraries(main B)

compiles ``main.c`` with ``-DA -DOBJ`` and links executable ``main``
to ``B`` and ``A``.  The object library's usage requirements are
propagated transitively through ``B``, but its object files are not.

:ref:`Object Libraries` may "link" to other object libraries to get
usage requirements, but since they do not have a link step nothing
is done with their object files.  Continuing from the above example,
the code:

.. code-block:: cmake

  add_library(obj2 OBJECT obj2.c)
  target_link_libraries(obj2 PUBLIC obj)

  add_executable(main2 main2.c)
  target_link_libraries(main2 obj2)

compiles ``obj2.c`` with ``-DA -DOBJ``, creates executable ``main2``
with object files from ``main2.c`` and ``obj2.c``, and links ``main2``
to ``A``.

In other words, when :ref:`Object Libraries` appear in a target's
:prop_tgt:`INTERFACE_LINK_LIBRARIES` property they will be
treated as :ref:`Interface Libraries`, but when they appear in
a target's :prop_tgt:`LINK_LIBRARIES` property their object files
will be included in the link too.

Cyclic Dependencies of Static Libraries
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The library dependency graph is normally acyclic (a DAG), but in the case
of mutually-dependent ``STATIC`` libraries CMake allows the graph to
contain cycles (strongly connected components).  When another target links
to one of the libraries, CMake repeats the entire connected component.
For example, the code

.. code-block:: cmake

  add_library(A STATIC a.c)
  add_library(B STATIC b.c)
  target_link_libraries(A B)
  target_link_libraries(B A)
  add_executable(main main.c)
  target_link_libraries(main A)

links ``main`` to ``A B A B``.  While one repetition is usually
sufficient, pathological object file and symbol arrangements can require
more.  One may handle such cases by using the
:prop_tgt:`LINK_INTERFACE_MULTIPLICITY` target property or by manually
repeating the component in the last ``target_link_libraries`` call.
However, if two archives are really so interdependent they should probably
be combined into a single archive, perhaps by using :ref:`Object Libraries`.

Creating Relocatable Packages
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. |INTERFACE_PROPERTY_LINK| replace:: :prop_tgt:`INTERFACE_LINK_LIBRARIES`
.. include:: /include/INTERFACE_LINK_LIBRARIES_WARNING.txt
