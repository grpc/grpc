CMAKE_CFG_INTDIR
----------------

Build-time reference to per-configuration output subdirectory.

For native build systems supporting multiple configurations in the
build tree (such as :ref:`Visual Studio Generators` and :generator:`Xcode`),
the value is a reference to a build-time variable specifying the name
of the per-configuration output subdirectory.  On :ref:`Makefile Generators`
this evaluates to `.` because there is only one configuration in a build tree.
Example values:

::

  $(ConfigurationName) = Visual Studio 9
  $(Configuration)     = Visual Studio 10
  $(CONFIGURATION)     = Xcode
  .                    = Make-based tools
  .                    = Ninja
  ${CONFIGURATION}     = Ninja Multi-Config

Note that this variable only has limited support on
:generator:`Ninja Multi-Config`. It is recommended that you use the
``$<CONFIG>`` :manual:`generator expression <cmake-generator-expressions(7)>`
instead.

Since these values are evaluated by the native build system, this
variable is suitable only for use in command lines that will be
evaluated at build time.  Example of intended usage:

::

  add_executable(mytool mytool.c)
  add_custom_command(
    OUTPUT out.txt
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/mytool
            ${CMAKE_CURRENT_SOURCE_DIR}/in.txt out.txt
    DEPENDS mytool in.txt
    )
  add_custom_target(drive ALL DEPENDS out.txt)

Note that ``CMAKE_CFG_INTDIR`` is no longer necessary for this purpose but
has been left for compatibility with existing projects.  Instead
:command:`add_custom_command` recognizes executable target names in its
``COMMAND`` option, so
``${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/mytool`` can be replaced
by just ``mytool``.

This variable is read-only.  Setting it is undefined behavior.  In
multi-configuration build systems the value of this variable is passed
as the value of preprocessor symbol ``CMAKE_INTDIR`` to the compilation
of all source files.
