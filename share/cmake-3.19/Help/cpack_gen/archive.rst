CPack Archive Generator
-----------------------

CPack generator for packaging files into an archive, which can have
any of the following formats:

  - 7Z - 7zip - (.7z)
  - TBZ2 (.tar.bz2)
  - TGZ (.tar.gz)
  - TXZ (.tar.xz)
  - TZ (.tar.Z)
  - TZST (.tar.zst)
  - ZIP (.zip)

When this generator is called from ``CPackSourceConfig.cmake`` (or through
the ``package_source`` target), then the generated archive will contain all
files in the project directory, except those specified in
:variable:`CPACK_SOURCE_IGNORE_FILES`.  The following is one example of
packaging all source files of a project:

.. code-block:: cmake

  set(CPACK_SOURCE_GENERATOR "TGZ")
  set(CPACK_SOURCE_IGNORE_FILES
    \\.git/
    build/
    ".*~$"
  )
  set(CPACK_VERBATIM_VARIABLES YES)
  include(CPack)

When this generator is called from ``CPackConfig.cmake`` (or through the
``package`` target), then the generated archive will contain all files
that have been installed via CMake's :command:`install` command (and the
deprecated commands :command:`install_files`, :command:`install_programs`,
and :command:`install_targets`).

Variables specific to CPack Archive generator
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. variable:: CPACK_ARCHIVE_FILE_NAME
              CPACK_ARCHIVE_<component>_FILE_NAME

  Package file name without extension. The extension is determined from the
  archive format (see list above) and automatically appended to the file name.
  The default is ``<CPACK_PACKAGE_FILE_NAME>[-<component>]``, with spaces
  replaced by '-'.

.. variable:: CPACK_ARCHIVE_COMPONENT_INSTALL

  Enable component packaging. If enabled (ON), then the archive generator
  creates  multiple packages. The default is OFF, which means that a single
  package containing files of all components is generated.

Variables used by CPack Archive generator
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

These variables are used by the Archive generator, but are also available to
CPack generators which are essentially archives at their core. These include:

  - :cpack_gen:`CPack Cygwin Generator`
  - :cpack_gen:`CPack FreeBSD Generator`

.. variable:: CPACK_ARCHIVE_THREADS

  The number of threads to use when performing the compression. If set to
  ``0``, the number of available cores on the machine will be used instead.
  The default is ``1`` which limits compression to a single thread. Note that
  not all compression modes support threading in all environments. Currently,
  only the XZ compression may support it.

.. note::

    Official CMake binaries available on ``cmake.org`` ship with a ``liblzma``
    that does not support parallel compression.
