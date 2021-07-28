CMAKE_INSTALL_PREFIX
--------------------

Install directory used by :command:`install`.

If ``make install`` is invoked or ``INSTALL`` is built, this directory is
prepended onto all install directories.  This variable defaults to
``/usr/local`` on UNIX and ``c:/Program Files/${PROJECT_NAME}`` on Windows.
See :variable:`CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT` for how a
project might choose its own default.

On UNIX one can use the ``DESTDIR`` mechanism in order to relocate the
whole installation. See :envvar:`DESTDIR` for more information.

The installation prefix is also added to :variable:`CMAKE_SYSTEM_PREFIX_PATH`
so that :command:`find_package`, :command:`find_program`,
:command:`find_library`, :command:`find_path`, and :command:`find_file`
will search the prefix for other software.

.. note::

  Use the :module:`GNUInstallDirs` module to provide GNU-style
  options for the layout of directories within the installation.
