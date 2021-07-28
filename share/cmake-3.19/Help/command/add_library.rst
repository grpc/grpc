add_library
-----------

.. only:: html

   .. contents::

Add a library to the project using the specified source files.

Normal Libraries
^^^^^^^^^^^^^^^^

.. code-block:: cmake

  add_library(<name> [STATIC | SHARED | MODULE]
              [EXCLUDE_FROM_ALL]
              [<source>...])

Adds a library target called ``<name>`` to be built from the source files
listed in the command invocation.  (The source files can be omitted here
if they are added later using :command:`target_sources`.)  The ``<name>``
corresponds to the logical target name and must be globally unique within
a project.  The actual file name of the library built is constructed based
on conventions of the native platform (such as ``lib<name>.a`` or
``<name>.lib``).

``STATIC``, ``SHARED``, or ``MODULE`` may be given to specify the type of
library to be created.  ``STATIC`` libraries are archives of object files
for use when linking other targets.  ``SHARED`` libraries are linked
dynamically and loaded at runtime.  ``MODULE`` libraries are plugins that
are not linked into other targets but may be loaded dynamically at runtime
using dlopen-like functionality.  If no type is given explicitly the
type is ``STATIC`` or ``SHARED`` based on whether the current value of the
variable :variable:`BUILD_SHARED_LIBS` is ``ON``.  For ``SHARED`` and
``MODULE`` libraries the :prop_tgt:`POSITION_INDEPENDENT_CODE` target
property is set to ``ON`` automatically.
A ``SHARED`` or ``STATIC`` library may be marked with the :prop_tgt:`FRAMEWORK`
target property to create an macOS Framework.

If a library does not export any symbols, it must not be declared as a
``SHARED`` library.  For example, a Windows resource DLL or a managed C++/CLI
DLL that exports no unmanaged symbols would need to be a ``MODULE`` library.
This is because CMake expects a ``SHARED`` library to always have an
associated import library on Windows.

By default the library file will be created in the build tree directory
corresponding to the source tree directory in which the command was
invoked.  See documentation of the :prop_tgt:`ARCHIVE_OUTPUT_DIRECTORY`,
:prop_tgt:`LIBRARY_OUTPUT_DIRECTORY`, and
:prop_tgt:`RUNTIME_OUTPUT_DIRECTORY` target properties to change this
location.  See documentation of the :prop_tgt:`OUTPUT_NAME` target
property to change the ``<name>`` part of the final file name.

If ``EXCLUDE_FROM_ALL`` is given the corresponding property will be set on
the created target.  See documentation of the :prop_tgt:`EXCLUDE_FROM_ALL`
target property for details.

Source arguments to ``add_library`` may use "generator expressions" with
the syntax ``$<...>``.  See the :manual:`cmake-generator-expressions(7)`
manual for available expressions.  See the :manual:`cmake-buildsystem(7)`
manual for more on defining buildsystem properties.

See also :prop_sf:`HEADER_FILE_ONLY` on what to do if some sources are
pre-processed, and you want to have the original sources reachable from
within IDE.

Object Libraries
^^^^^^^^^^^^^^^^

.. code-block:: cmake

  add_library(<name> OBJECT [<source>...])

Creates an :ref:`Object Library <Object Libraries>`.  An object library
compiles source files but does not archive or link their object files into a
library.  Instead other targets created by :command:`add_library` or
:command:`add_executable` may reference the objects using an expression of the
form ``$<TARGET_OBJECTS:objlib>`` as a source, where ``objlib`` is the
object library name.  For example:

.. code-block:: cmake

  add_library(... $<TARGET_OBJECTS:objlib> ...)
  add_executable(... $<TARGET_OBJECTS:objlib> ...)

will include objlib's object files in a library and an executable
along with those compiled from their own sources.  Object libraries
may contain only sources that compile, header files, and other files
that would not affect linking of a normal library (e.g. ``.txt``).
They may contain custom commands generating such sources, but not
``PRE_BUILD``, ``PRE_LINK``, or ``POST_BUILD`` commands.  Some native build
systems (such as Xcode) may not like targets that have only object files, so
consider adding at least one real source file to any target that references
``$<TARGET_OBJECTS:objlib>``.

Interface Libraries
^^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

  add_library(<name> INTERFACE)

Creates an :ref:`Interface Library <Interface Libraries>`.
An ``INTERFACE`` library target does not compile sources and does
not produce a library artifact on disk.  However, it may have
properties set on it and it may be installed and exported.
Typically, ``INTERFACE_*`` properties are populated on an interface
target using the commands:

* :command:`set_property`,
* :command:`target_link_libraries(INTERFACE)`,
* :command:`target_link_options(INTERFACE)`,
* :command:`target_include_directories(INTERFACE)`,
* :command:`target_compile_options(INTERFACE)`,
* :command:`target_compile_definitions(INTERFACE)`, and
* :command:`target_sources(INTERFACE)`,

and then it is used as an argument to :command:`target_link_libraries`
like any other target.

An interface library created with the above signature has no source files
itself and is not included as a target in the generated buildsystem.

Since CMake 3.19, an interface library target may be created with
source files:

.. code-block:: cmake

  add_library(<name> INTERFACE [<source>...] [EXCLUDE_FROM_ALL])

Source files may be listed directly in the ``add_library`` call or added
later by calls to :command:`target_sources` with the ``PRIVATE`` or
``PUBLIC`` keywords.

If an interface library has source files (i.e. the :prop_tgt:`SOURCES`
target property is set), it will appear in the generated buildsystem
as a build target much like a target defined by the
:command:`add_custom_target` command.  It does not compile any sources,
but does contain build rules for custom commands created by the
:command:`add_custom_command` command.

.. note::
  In most command signatures where the ``INTERFACE`` keyword appears,
  the items listed after it only become part of that target's usage
  requirements and are not part of the target's own settings.  However,
  in this signature of ``add_library``, the ``INTERFACE`` keyword refers
  to the library type only.  Sources listed after it in the ``add_library``
  call are ``PRIVATE`` to the interface library and do not appear in its
  :prop_tgt:`INTERFACE_SOURCES` target property.

Imported Libraries
^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

  add_library(<name> <type> IMPORTED [GLOBAL])

Creates an :ref:`IMPORTED library target <Imported Targets>` called ``<name>``.
No rules are generated to build it, and the :prop_tgt:`IMPORTED` target
property is ``True``.  The target name has scope in the directory in which
it is created and below, but the ``GLOBAL`` option extends visibility.
It may be referenced like any target built within the project.
``IMPORTED`` libraries are useful for convenient reference from commands
like :command:`target_link_libraries`.  Details about the imported library
are specified by setting properties whose names begin in ``IMPORTED_`` and
``INTERFACE_``.

The ``<type>`` must be one of:

``STATIC``, ``SHARED``, ``MODULE``, ``UNKNOWN``
  References a library file located outside the project.  The
  :prop_tgt:`IMPORTED_LOCATION` target property (or its per-configuration
  variant :prop_tgt:`IMPORTED_LOCATION_<CONFIG>`) specifies the
  location of the main library file on disk.  In the case of a ``SHARED``
  library on Windows, the :prop_tgt:`IMPORTED_IMPLIB` target property
  (or its per-configuration variant :prop_tgt:`IMPORTED_IMPLIB_<CONFIG>`)
  specifies the location of the DLL import library file (``.lib`` or
  ``.dll.a``) on disk, and the ``IMPORTED_LOCATION`` is the location of
  the ``.dll`` runtime library (and is optional).
  Additional usage requirements may be specified in ``INTERFACE_*`` properties.

  An ``UNKNOWN`` library type is typically only used in the implementation of
  :ref:`Find Modules`.  It allows the path to an imported library (often found
  using the :command:`find_library` command) to be used without having to know
  what type of library it is.  This is especially useful on Windows where a
  static library and a DLL's import library both have the same file extension.

``OBJECT``
  References a set of object files located outside the project.
  The :prop_tgt:`IMPORTED_OBJECTS` target property (or its per-configuration
  variant :prop_tgt:`IMPORTED_OBJECTS_<CONFIG>`) specifies the locations of
  object files on disk.
  Additional usage requirements may be specified in ``INTERFACE_*`` properties.

``INTERFACE``
  Does not reference any library or object files on disk, but may
  specify usage requirements in ``INTERFACE_*`` properties.

See documentation of the ``IMPORTED_*`` and ``INTERFACE_*`` properties
for more information.

Alias Libraries
^^^^^^^^^^^^^^^

.. code-block:: cmake

  add_library(<name> ALIAS <target>)

Creates an :ref:`Alias Target <Alias Targets>`, such that ``<name>`` can be
used to refer to ``<target>`` in subsequent commands.  The ``<name>`` does
not appear in the generated buildsystem as a make target.  The ``<target>``
may not be an ``ALIAS``.

An ``ALIAS`` to a non-``GLOBAL`` :ref:`Imported Target <Imported Targets>`
has scope in the directory in which the alias is created and below.
The :prop_tgt:`ALIAS_GLOBAL` target property can be used to check if the
alias is global or not.

``ALIAS`` targets can be used as linkable targets and as targets to
read properties from.  They can also be tested for existence with the
regular :command:`if(TARGET)` subcommand.  The ``<name>`` may not be used
to modify properties of ``<target>``, that is, it may not be used as the
operand of :command:`set_property`, :command:`set_target_properties`,
:command:`target_link_libraries` etc.  An ``ALIAS`` target may not be
installed or exported.
