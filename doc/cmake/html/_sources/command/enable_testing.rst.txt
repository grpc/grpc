enable_testing
--------------

Enable testing for current directory and below.

.. code-block:: cmake

  enable_testing()

Enables testing for this directory and below.

This command should be in the source directory root
because ctest expects to find a test file in the build
directory root.

This command is automatically invoked when the :module:`CTest`
module is included, except if the ``BUILD_TESTING`` option is
turned off.

See also the :command:`add_test` command.
