try_run
-------

.. only:: html

   .. contents::

Try compiling and then running some code.

Try Compiling and Running Source Files
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

  try_run(<runResultVar> <compileResultVar>
          <bindir> <srcfile> [CMAKE_FLAGS <flags>...]
          [COMPILE_DEFINITIONS <defs>...]
          [LINK_OPTIONS <options>...]
          [LINK_LIBRARIES <libs>...]
          [COMPILE_OUTPUT_VARIABLE <var>]
          [RUN_OUTPUT_VARIABLE <var>]
          [OUTPUT_VARIABLE <var>]
          [ARGS <args>...])

Try compiling a ``<srcfile>``.  Returns ``TRUE`` or ``FALSE`` for success
or failure in ``<compileResultVar>``.  If the compile succeeded, runs the
executable and returns its exit code in ``<runResultVar>``.  If the
executable was built, but failed to run, then ``<runResultVar>`` will be
set to ``FAILED_TO_RUN``.  See the :command:`try_compile` command for
information on how the test project is constructed to build the source file.

The options are:

``CMAKE_FLAGS <flags>...``
  Specify flags of the form ``-DVAR:TYPE=VALUE`` to be passed to
  the ``cmake`` command-line used to drive the test build.
  The example in :command:`try_compile` shows how values for variables
  ``INCLUDE_DIRECTORIES``, ``LINK_DIRECTORIES``, and ``LINK_LIBRARIES``
  are used.

``COMPILE_DEFINITIONS <defs>...``
  Specify ``-Ddefinition`` arguments to pass to :command:`add_definitions`
  in the generated test project.

``COMPILE_OUTPUT_VARIABLE <var>``
  Report the compile step build output in a given variable.

``LINK_LIBRARIES <libs>...``
  Specify libraries to be linked in the generated project.
  The list of libraries may refer to system libraries and to
  :ref:`Imported Targets <Imported Targets>` from the calling project.

  If this option is specified, any ``-DLINK_LIBRARIES=...`` value
  given to the ``CMAKE_FLAGS`` option will be ignored.

``LINK_OPTIONS <options>...``
  Specify link step options to pass to :command:`target_link_options` in the
  generated project.

``OUTPUT_VARIABLE <var>``
  Report the compile build output and the output from running the executable
  in the given variable.  This option exists for legacy reasons.  Prefer
  ``COMPILE_OUTPUT_VARIABLE`` and ``RUN_OUTPUT_VARIABLE`` instead.

``RUN_OUTPUT_VARIABLE <var>``
  Report the output from running the executable in a given variable.

Other Behavior Settings
^^^^^^^^^^^^^^^^^^^^^^^

Set the :variable:`CMAKE_TRY_COMPILE_CONFIGURATION` variable to choose
a build configuration.

Behavior when Cross Compiling
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When cross compiling, the executable compiled in the first step
usually cannot be run on the build host.  The ``try_run`` command checks
the :variable:`CMAKE_CROSSCOMPILING` variable to detect whether CMake is in
cross-compiling mode.  If that is the case, it will still try to compile
the executable, but it will not try to run the executable unless the
:variable:`CMAKE_CROSSCOMPILING_EMULATOR` variable is set.  Instead it
will create cache variables which must be filled by the user or by
presetting them in some CMake script file to the values the executable
would have produced if it had been run on its actual target platform.
These cache entries are:

``<runResultVar>``
  Exit code if the executable were to be run on the target platform.

``<runResultVar>__TRYRUN_OUTPUT``
  Output from stdout and stderr if the executable were to be run on
  the target platform.  This is created only if the
  ``RUN_OUTPUT_VARIABLE`` or ``OUTPUT_VARIABLE`` option was used.

In order to make cross compiling your project easier, use ``try_run``
only if really required.  If you use ``try_run``, use the
``RUN_OUTPUT_VARIABLE`` or ``OUTPUT_VARIABLE`` options only if really
required.  Using them will require that when cross-compiling, the cache
variables will have to be set manually to the output of the executable.
You can also "guard" the calls to ``try_run`` with an :command:`if`
block checking the :variable:`CMAKE_CROSSCOMPILING` variable and
provide an easy-to-preset alternative for this case.
