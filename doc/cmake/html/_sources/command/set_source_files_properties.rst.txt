set_source_files_properties
---------------------------

Source files can have properties that affect how they are built.

.. code-block:: cmake

  set_source_files_properties(<files> ...
                              [DIRECTORY <dirs> ...]
                              [TARGET_DIRECTORY <targets> ...]
                              PROPERTIES <prop1> <value1>
                              [<prop2> <value2>] ...)

Sets properties associated with source files using a key/value paired
list.

By default, source file properties are only visible to targets added in the
same directory (``CMakeLists.txt``).  Visibility can be set in other directory
scopes using one or both of the following options:

``DIRECTORY <dirs>...``
  The source file properties will be set in each of the ``<dirs>``
  directories' scopes.  CMake must already know about each of these
  source directories, either by having added them through a call to
  :command:`add_subdirectory` or it being the top level source directory.
  Relative paths are treated as relative to the current source directory.

``TARGET_DIRECTORY <targets>...``
  The source file properties will be set in each of the directory scopes
  where any of the specified ``<targets>`` were created (the ``<targets>``
  must therefore already exist).

Use :command:`get_source_file_property` to get property values.
See also the :command:`set_property(SOURCE)` command.

See :ref:`Source File Properties` for the list of properties known
to CMake.
