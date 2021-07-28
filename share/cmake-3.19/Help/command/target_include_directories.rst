target_include_directories
--------------------------

Add include directories to a target.

.. code-block:: cmake

  target_include_directories(<target> [SYSTEM] [BEFORE]
    <INTERFACE|PUBLIC|PRIVATE> [items1...]
    [<INTERFACE|PUBLIC|PRIVATE> [items2...] ...])

Specifies include directories to use when compiling a given target.
The named ``<target>`` must have been created by a command such
as :command:`add_executable` or :command:`add_library` and must not be an
:ref:`ALIAS target <Alias Targets>`.

If ``BEFORE`` is specified, the content will be prepended to the property
instead of being appended.

The ``INTERFACE``, ``PUBLIC`` and ``PRIVATE`` keywords are required to specify
the scope of the following arguments.  ``PRIVATE`` and ``PUBLIC`` items will
populate the :prop_tgt:`INCLUDE_DIRECTORIES` property of ``<target>``.
``PUBLIC`` and ``INTERFACE`` items will populate the
:prop_tgt:`INTERFACE_INCLUDE_DIRECTORIES` property of ``<target>``.
(:ref:`IMPORTED targets <Imported Targets>` only support ``INTERFACE`` items.)
The following arguments specify include directories.

Specified include directories may be absolute paths or relative paths.
Repeated calls for the same <target> append items in the order called.  If
``SYSTEM`` is specified, the compiler will be told the
directories are meant as system include directories on some platforms
(signalling this setting might achieve effects such as the compiler
skipping warnings, or these fixed-install system files not being
considered in dependency calculations - see compiler docs).  If ``SYSTEM``
is used together with ``PUBLIC`` or ``INTERFACE``, the
:prop_tgt:`INTERFACE_SYSTEM_INCLUDE_DIRECTORIES` target property will be
populated with the specified directories.

Arguments to ``target_include_directories`` may use "generator expressions"
with the syntax ``$<...>``.  See the :manual:`cmake-generator-expressions(7)`
manual for available expressions.  See the :manual:`cmake-buildsystem(7)`
manual for more on defining buildsystem properties.

Include directories usage requirements commonly differ between the build-tree
and the install-tree.  The ``BUILD_INTERFACE`` and ``INSTALL_INTERFACE``
generator expressions can be used to describe separate usage requirements
based on the usage location.  Relative paths are allowed within the
``INSTALL_INTERFACE`` expression and are interpreted relative to the
installation prefix.  For example:

.. code-block:: cmake

  target_include_directories(mylib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/mylib>
    $<INSTALL_INTERFACE:include/mylib>  # <prefix>/include/mylib
  )

Creating Relocatable Packages
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. |INTERFACE_PROPERTY_LINK| replace:: :prop_tgt:`INTERFACE_INCLUDE_DIRECTORIES`
.. include:: /include/INTERFACE_INCLUDE_DIRECTORIES_WARNING.txt
