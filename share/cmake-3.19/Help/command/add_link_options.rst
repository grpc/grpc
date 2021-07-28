add_link_options
----------------

.. versionadded:: 3.13

Add options to the link step for executable, shared library or module
library targets in the current directory and below that are added after
this command is invoked.

.. code-block:: cmake

  add_link_options(<option> ...)

This command can be used to add any link options, but alternative commands
exist to add libraries (:command:`target_link_libraries` or
:command:`link_libraries`).  See documentation of the
:prop_dir:`directory <LINK_OPTIONS>` and
:prop_tgt:`target <LINK_OPTIONS>` ``LINK_OPTIONS`` properties.

.. note::

  This command cannot be used to add options for static library targets,
  since they do not use a linker.  To add archiver or MSVC librarian flags,
  see the :prop_tgt:`STATIC_LIBRARY_OPTIONS` target property.

Arguments to ``add_link_options`` may use "generator expressions" with
the syntax ``$<...>``.  See the :manual:`cmake-generator-expressions(7)`
manual for available expressions.  See the :manual:`cmake-buildsystem(7)`
manual for more on defining buildsystem properties.

.. include:: DEVICE_LINK_OPTIONS.txt

.. include:: OPTIONS_SHELL.txt

.. include:: LINK_OPTIONS_LINKER.txt
