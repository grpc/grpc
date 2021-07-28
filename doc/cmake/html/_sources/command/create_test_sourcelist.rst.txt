create_test_sourcelist
----------------------

Create a test driver and source list for building test programs.

.. code-block:: cmake

  create_test_sourcelist(sourceListName driverName
                         test1 test2 test3
                         EXTRA_INCLUDE include.h
                         FUNCTION function)

A test driver is a program that links together many small tests into a
single executable.  This is useful when building static executables
with large libraries to shrink the total required size.  The list of
source files needed to build the test driver will be in
``sourceListName``.  ``driverName`` is the name of the test driver program.
The rest of the arguments consist of a list of test source files, can
be semicolon separated.  Each test source file should have a function
in it that is the same name as the file with no extension (foo.cxx
should have int foo(int, char*[]);) ``driverName`` will be able to call
each of the tests by name on the command line.  If ``EXTRA_INCLUDE`` is
specified, then the next argument is included into the generated file.
If ``FUNCTION`` is specified, then the next argument is taken as a
function name that is passed a pointer to ac and av.  This can be used
to add extra command line processing to each test.  The
``CMAKE_TESTDRIVER_BEFORE_TESTMAIN`` cmake variable can be set to
have code that will be placed directly before calling the test main function.
``CMAKE_TESTDRIVER_AFTER_TESTMAIN`` can be set to have code that
will be placed directly after the call to the test main function.
