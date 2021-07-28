CMake Tutorial
**************

.. only:: html

   .. contents::

Introduction
============

The CMake tutorial provides a step-by-step guide that covers common build
system issues that CMake helps address. Seeing how various topics all
work together in an example project can be very helpful. The tutorial
documentation and source code for examples can be found in the
``Help/guide/tutorial`` directory of the CMake source code tree. Each step has
its own subdirectory containing code that may be used as a starting point. The
tutorial examples are progressive so that each step provides the complete
solution for the previous step.

A Basic Starting Point (Step 1)
===============================

The most basic project is an executable built from source code files.
For simple projects, a three line ``CMakeLists.txt`` file is all that is
required. This will be the starting point for our tutorial. Create a
``CMakeLists.txt`` file in the ``Step1`` directory that looks like:

.. code-block:: cmake

  cmake_minimum_required(VERSION 3.10)

  # set the project name
  project(Tutorial)

  # add the executable
  add_executable(Tutorial tutorial.cxx)


Note that this example uses lower case commands in the ``CMakeLists.txt`` file.
Upper, lower, and mixed case commands are supported by CMake. The source
code for ``tutorial.cxx`` is provided in the ``Step1`` directory and can be
used to compute the square root of a number.

Adding a Version Number and Configured Header File
--------------------------------------------------

The first feature we will add is to provide our executable and project with a
version number. While we could do this exclusively in the source code, using
``CMakeLists.txt`` provides more flexibility.

First, modify the ``CMakeLists.txt`` file to use the :command:`project` command
to set the project name and version number.

.. literalinclude:: Step2/CMakeLists.txt
  :language: cmake
  :end-before: # specify the C++ standard

Then, configure a header file to pass the version number to the source
code:

.. literalinclude:: Step2/CMakeLists.txt
  :language: cmake
  :start-after: # to the source code
  :end-before: # add the executable

Since the configured file will be written into the binary tree, we
must add that directory to the list of paths to search for include
files. Add the following lines to the end of the ``CMakeLists.txt`` file:

.. literalinclude:: Step2/CMakeLists.txt
  :language: cmake
  :start-after: # so that we will find TutorialConfig.h

Using your favorite editor, create ``TutorialConfig.h.in`` in the source
directory with the following contents:

.. literalinclude:: Step2/TutorialConfig.h.in
  :language: cmake

When CMake configures this header file the values for
``@Tutorial_VERSION_MAJOR@`` and ``@Tutorial_VERSION_MINOR@`` will be
replaced.

Next modify ``tutorial.cxx`` to include the configured header file,
``TutorialConfig.h``.

Finally, let's print out the executable name and version number by updating
``tutorial.cxx`` as follows:

.. literalinclude:: Step2/tutorial.cxx
  :language: c++
  :start-after: {
  :end-before: // convert input to double

Specify the C++ Standard
-------------------------

Next let's add some C++11 features to our project by replacing ``atof`` with
``std::stod`` in ``tutorial.cxx``.  At the same time, remove
``#include <cstdlib>``.

.. literalinclude:: Step2/tutorial.cxx
  :language: c++
  :start-after: // convert input to double
  :end-before: // calculate square root

We will need to explicitly state in the CMake code that it should use the
correct flags. The easiest way to enable support for a specific C++ standard
in CMake is by using the :variable:`CMAKE_CXX_STANDARD` variable. For this
tutorial, set the :variable:`CMAKE_CXX_STANDARD` variable in the
``CMakeLists.txt`` file to 11 and :variable:`CMAKE_CXX_STANDARD_REQUIRED` to
True. Make sure to add the ``CMAKE_CXX_STANDARD`` declarations above the call
to ``add_executable``.

.. literalinclude:: Step2/CMakeLists.txt
  :language: cmake
  :end-before: # configure a header file to pass some of the CMake settings

Build and Test
--------------

Run the :manual:`cmake <cmake(1)>` executable or the
:manual:`cmake-gui <cmake-gui(1)>` to configure the project and then build it
with your chosen build tool.

For example, from the command line we could navigate to the
``Help/guide/tutorial`` directory of the CMake source code tree and create a
build directory:

.. code-block:: console

  mkdir Step1_build

Next, navigate to the build directory and run CMake to configure the project
and generate a native build system:

.. code-block:: console

  cd Step1_build
  cmake ../Step1

Then call that build system to actually compile/link the project:

.. code-block:: console

  cmake --build .

Finally, try to use the newly built ``Tutorial`` with these commands:

.. code-block:: console

  Tutorial 4294967296
  Tutorial 10
  Tutorial

Adding a Library (Step 2)
=========================

Now we will add a library to our project. This library will contain our own
implementation for computing the square root of a number. The executable can
then use this library instead of the standard square root function provided by
the compiler.

For this tutorial we will put the library into a subdirectory
called ``MathFunctions``. This directory already contains a header file,
``MathFunctions.h``, and a source file ``mysqrt.cxx``. The source file has one
function called ``mysqrt`` that provides similar functionality to the
compiler's ``sqrt`` function.

Add the following one line ``CMakeLists.txt`` file to the ``MathFunctions``
directory:

.. literalinclude:: Step3/MathFunctions/CMakeLists.txt
  :language: cmake

To make use of the new library we will add an :command:`add_subdirectory`
call in the top-level ``CMakeLists.txt`` file so that the library will get
built. We add the new library to the executable, and add ``MathFunctions`` as
an include directory so that the ``mqsqrt.h`` header file can be found. The
last few lines of the top-level ``CMakeLists.txt`` file should now look like:

.. code-block:: cmake

        # add the MathFunctions library
        add_subdirectory(MathFunctions)

        # add the executable
        add_executable(Tutorial tutorial.cxx)

        target_link_libraries(Tutorial PUBLIC MathFunctions)

        # add the binary tree to the search path for include files
        # so that we will find TutorialConfig.h
        target_include_directories(Tutorial PUBLIC
                                  "${PROJECT_BINARY_DIR}"
                                  "${PROJECT_SOURCE_DIR}/MathFunctions"
                                  )

Now let us make the MathFunctions library optional. While for the tutorial
there really isn't any need to do so, for larger projects this is a common
occurrence. The first step is to add an option to the top-level
``CMakeLists.txt`` file.

.. literalinclude:: Step3/CMakeLists.txt
  :language: cmake
  :start-after: # should we use our own math functions
  :end-before: # add the MathFunctions library

This option will be displayed in the :manual:`cmake-gui <cmake-gui(1)>` and
:manual:`ccmake <ccmake(1)>`
with a default value of ON that can be changed by the user. This setting will
be stored in the cache so that the user does not need to set the value each
time they run CMake on a build directory.

The next change is to make building and linking the MathFunctions library
conditional. To do this we change the end of the top-level ``CMakeLists.txt``
file to look like the following:

.. literalinclude:: Step3/CMakeLists.txt
  :language: cmake
  :start-after: # add the MathFunctions library

Note the use of the variable ``EXTRA_LIBS`` to collect up any optional
libraries to later be linked into the executable. The variable
``EXTRA_INCLUDES`` is used similarly for optional header files. This is a
classic approach when dealing with many optional components, we will cover
the modern approach in the next step.

The corresponding changes to the source code are fairly straightforward.
First, in ``tutorial.cxx``, include the ``MathFunctions.h`` header if we
need it:

.. literalinclude:: Step3/tutorial.cxx
  :language: c++
  :start-after: // should we include the MathFunctions header
  :end-before: int main

Then, in the same file, make ``USE_MYMATH`` control which square root
function is used:

.. literalinclude:: Step3/tutorial.cxx
  :language: c++
  :start-after: // which square root function should we use?
  :end-before: std::cout << "The square root of

Since the source code now requires ``USE_MYMATH`` we can add it to
``TutorialConfig.h.in`` with the following line:

.. literalinclude:: Step3/TutorialConfig.h.in
  :language: c
  :lines: 4

**Exercise**: Why is it important that we configure ``TutorialConfig.h.in``
after the option for ``USE_MYMATH``? What would happen if we inverted the two?

Run the :manual:`cmake  <cmake(1)>` executable or the
:manual:`cmake-gui <cmake-gui(1)>` to configure the project and then build it
with your chosen build tool. Then run the built Tutorial executable.

Now let's update the value of ``USE_MYMATH``. The easiest way is to use the
:manual:`cmake-gui <cmake-gui(1)>` or  :manual:`ccmake <ccmake(1)>` if you're
in the terminal. Or, alternatively, if you want to change the option from the
command-line, try:

.. code-block:: console

  cmake ../Step2 -DUSE_MYMATH=OFF

Rebuild and run the tutorial again.

Which function gives better results, sqrt or mysqrt?

Adding Usage Requirements for Library (Step 3)
==============================================

Usage requirements allow for far better control over a library or executable's
link and include line while also giving more control over the transitive
property of targets inside CMake. The primary commands that leverage usage
requirements are:

  - :command:`target_compile_definitions`
  - :command:`target_compile_options`
  - :command:`target_include_directories`
  - :command:`target_link_libraries`

Let's refactor our code from `Adding a Library (Step 2)`_ to use the modern
CMake approach of usage requirements. We first state that anybody linking to
MathFunctions needs to include the current source directory, while
MathFunctions itself doesn't. So this can become an ``INTERFACE`` usage
requirement.

Remember ``INTERFACE`` means things that consumers require but the producer
doesn't. Add the following lines to the end of
``MathFunctions/CMakeLists.txt``:

.. literalinclude:: Step4/MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # to find MathFunctions.h

Now that we've specified usage requirements for MathFunctions we can safely
remove our uses of the ``EXTRA_INCLUDES`` variable from the top-level
``CMakeLists.txt``, here:

.. literalinclude:: Step4/CMakeLists.txt
  :language: cmake
  :start-after: # add the MathFunctions library
  :end-before: # add the executable

And here:

.. literalinclude:: Step4/CMakeLists.txt
  :language: cmake
  :start-after: # so that we will find TutorialConfig.h

Once this is done, run the :manual:`cmake  <cmake(1)>` executable or the
:manual:`cmake-gui <cmake-gui(1)>` to configure the project and then build it
with your chosen build tool or by using ``cmake --build .`` from the build
directory.

Installing and Testing (Step 4)
===============================

Now we can start adding install rules and testing support to our project.

Install Rules
-------------

The install rules are fairly simple: for MathFunctions we want to install the
library and header file and for the application we want to install the
executable and configured header.

So to the end of ``MathFunctions/CMakeLists.txt`` we add:

.. literalinclude:: Step5/MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # install rules

And to the end of the top-level ``CMakeLists.txt`` we add:

.. literalinclude:: Step5/CMakeLists.txt
  :language: cmake
  :start-after: # add the install targets
  :end-before: # enable testing

That is all that is needed to create a basic local install of the tutorial.

Now run the :manual:`cmake  <cmake(1)>` executable or the
:manual:`cmake-gui <cmake-gui(1)>` to configure the project and then build it
with your chosen build tool.

Then run the install step by using the ``install`` option of the
:manual:`cmake  <cmake(1)>` command (introduced in 3.15, older versions of
CMake must use ``make install``) from the command line. For
multi-configuration tools, don't forget to use the ``--config`` argument to
specify the configuration. If using an IDE, simply build the ``INSTALL``
target. This step will install the appropriate header files, libraries, and
executables. For example:

.. code-block:: console

  cmake --install .

The CMake variable :variable:`CMAKE_INSTALL_PREFIX` is used to determine the
root of where the files will be installed. If using the ``cmake --install``
command, the installation prefix can be overridden via the ``--prefix``
argument. For example:

.. code-block:: console

  cmake --install . --prefix "/home/myuser/installdir"

Navigate to the install directory and verify that the installed Tutorial runs.

Testing Support
---------------

Next let's test our application. At the end of the top-level ``CMakeLists.txt``
file we can enable testing and then add a number of basic tests to verify that
the application is working correctly.

.. literalinclude:: Step5/CMakeLists.txt
  :language: cmake
  :start-after: # enable testing

The first test simply verifies that the application runs, does not segfault or
otherwise crash, and has a zero return value. This is the basic form of a
CTest test.

The next test makes use of the :prop_test:`PASS_REGULAR_EXPRESSION` test
property to verify that the output of the test contains certain strings. In
this case, verifying that the usage message is printed when an incorrect number
of arguments are provided.

Lastly, we have a function called ``do_test`` that runs the application and
verifies that the computed square root is correct for given input. For each
invocation of ``do_test``, another test is added to the project with a name,
input, and expected results based on the passed arguments.

Rebuild the application and then cd to the binary directory and run the
:manual:`ctest <ctest(1)>` executable: ``ctest -N`` and ``ctest -VV``. For
multi-config generators (e.g. Visual Studio), the configuration type must be
specified. To run tests in Debug mode, for example, use ``ctest -C Debug -VV``
from the build directory (not the Debug subdirectory!). Alternatively, build
the ``RUN_TESTS`` target from the IDE.

Adding System Introspection (Step 5)
====================================

Let us consider adding some code to our project that depends on features the
target platform may not have. For this example, we will add some code that
depends on whether or not the target platform has the ``log`` and ``exp``
functions. Of course almost every platform has these functions but for this
tutorial assume that they are not common.

If the platform has ``log`` and ``exp`` then we will use them to compute the
square root in the ``mysqrt`` function. We first test for the availability of
these functions using the :module:`CheckSymbolExists` module in the top-level
``CMakeLists.txt``. On some platforms, we will need to link to the m library.
If ``log`` and ``exp`` are not initially found, require the m library and try
again.

We're going to use the new defines in ``TutorialConfig.h.in``, so be sure to
set them before that file is configured.

.. literalinclude:: Step6/MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # does this system provide the log and exp functions?
  :end-before: # add compile definitions

Now let's add these defines to ``TutorialConfig.h.in`` so that we can use them
from ``mysqrt.cxx``:

.. code-block:: console

  // does the platform provide exp and log functions?
  #cmakedefine HAVE_LOG
  #cmakedefine HAVE_EXP

If ``log`` and ``exp`` are available on the system, then we will use them to
compute the square root in the ``mysqrt`` function. Add the following code to
the ``mysqrt`` function in ``MathFunctions/mysqrt.cxx`` (don't forget the
``#endif`` before returning the result!):

.. literalinclude:: Step6/MathFunctions/mysqrt.cxx
  :language: c++
  :start-after: // if we have both log and exp then use them
  :end-before: // do ten iterations

We will also need to modify ``mysqrt.cxx`` to include ``cmath``.

.. literalinclude:: Step6/MathFunctions/mysqrt.cxx
  :language: c++
  :end-before: #include <iostream>

Run the :manual:`cmake  <cmake(1)>` executable or the
:manual:`cmake-gui <cmake-gui(1)>` to configure the project and then build it
with your chosen build tool and run the Tutorial executable.

You will notice that we're not using ``log`` and ``exp``, even if we think they
should be available. We should realize quickly that we have forgotten to
include ``TutorialConfig.h`` in ``mysqrt.cxx``.

We will also need to update ``MathFunctions/CMakeLists.txt`` so ``mysqrt.cxx``
knows where this file is located:

.. code-block:: cmake

  target_include_directories(MathFunctions
            INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
            PRIVATE ${CMAKE_BINARY_DIR}
            )

After making this update, go ahead and build the project again and run the
built Tutorial executable. If ``log`` and ``exp`` are still not being used,
open the generated ``TutorialConfig.h`` file from the build directory. Maybe
they aren't available on the current system?

Which function gives better results now, sqrt or mysqrt?

Specify Compile Definition
--------------------------

Is there a better place for us to save the ``HAVE_LOG`` and ``HAVE_EXP`` values
other than in ``TutorialConfig.h``? Let's try to use
:command:`target_compile_definitions`.

First, remove the defines from ``TutorialConfig.h.in``. We no longer need to
include ``TutorialConfig.h`` from ``mysqrt.cxx`` or the extra include in
``MathFunctions/CMakeLists.txt``.

Next, we can move the check for ``HAVE_LOG`` and ``HAVE_EXP`` to
``MathFunctions/CMakeLists.txt`` and then specify those values as ``PRIVATE``
compile definitions.

.. literalinclude:: Step6/MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # does this system provide the log and exp functions?
  :end-before: # install rules

After making these updates, go ahead and build the project again. Run the
built Tutorial executable and verify that the results are same as earlier in
this step.

Adding a Custom Command and Generated File (Step 6)
===================================================

Suppose, for the purpose of this tutorial, we decide that we never want to use
the platform ``log`` and ``exp`` functions and instead would like to
generate a table of precomputed values to use in the ``mysqrt`` function.
In this section, we will create the table as part of the build process,
and then compile that table into our application.

First, let's remove the check for the ``log`` and ``exp`` functions in
``MathFunctions/CMakeLists.txt``. Then remove the check for ``HAVE_LOG`` and
``HAVE_EXP`` from ``mysqrt.cxx``. At the same time, we can remove
:code:`#include <cmath>`.

In the ``MathFunctions`` subdirectory, a new source file named
``MakeTable.cxx`` has been provided to generate the table.

After reviewing the file, we can see that the table is produced as valid C++
code and that the output filename is passed in as an argument.

The next step is to add the appropriate commands to the
``MathFunctions/CMakeLists.txt`` file to build the MakeTable executable and
then run it as part of the build process. A few commands are needed to
accomplish this.

First, at the top of ``MathFunctions/CMakeLists.txt``, the executable for
``MakeTable`` is added as any other executable would be added.

.. literalinclude:: Step7/MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # first we add the executable that generates the table
  :end-before: # add the command to generate the source code

Then we add a custom command that specifies how to produce ``Table.h``
by running MakeTable.

.. literalinclude:: Step7/MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # add the command to generate the source code
  :end-before: # add the main library

Next we have to let CMake know that ``mysqrt.cxx`` depends on the generated
file ``Table.h``. This is done by adding the generated ``Table.h`` to the list
of sources for the library MathFunctions.

.. literalinclude:: Step7/MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # add the main library
  :end-before: # state that anybody linking

We also have to add the current binary directory to the list of include
directories so that ``Table.h`` can be found and included by ``mysqrt.cxx``.

.. literalinclude:: Step7/MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # state that we depend on our bin
  :end-before: # install rules

Now let's use the generated table. First, modify ``mysqrt.cxx`` to include
``Table.h``. Next, we can rewrite the mysqrt function to use the table:

.. literalinclude:: Step7/MathFunctions/mysqrt.cxx
  :language: c++
  :start-after: // a hack square root calculation using simple operations

Run the :manual:`cmake  <cmake(1)>` executable or the
:manual:`cmake-gui <cmake-gui(1)>` to configure the project and then build it
with your chosen build tool.

When this project is built it will first build the ``MakeTable`` executable.
It will then run ``MakeTable`` to produce ``Table.h``. Finally, it will
compile ``mysqrt.cxx`` which includes ``Table.h`` to produce the MathFunctions
library.

Run the Tutorial executable and verify that it is using the table.

Building an Installer (Step 7)
==============================

Next suppose that we want to distribute our project to other people so that
they can use it. We want to provide both binary and source distributions on a
variety of platforms. This is a little different from the install we did
previously in `Installing and Testing (Step 4)`_ , where we were
installing the binaries that we had built from the source code. In this
example we will be building installation packages that support binary
installations and package management features. To accomplish this we will use
CPack to create platform specific installers. Specifically we need to add a
few lines to the bottom of our top-level ``CMakeLists.txt`` file.

.. literalinclude:: Step8/CMakeLists.txt
  :language: cmake
  :start-after: # setup installer

That is all there is to it. We start by including
:module:`InstallRequiredSystemLibraries`. This module will include any runtime
libraries that are needed by the project for the current platform. Next we set
some CPack variables to where we have stored the license and version
information for this project. The version information was set earlier in this
tutorial and the ``license.txt`` has been included in the top-level source
directory for this step.

Finally we include the :module:`CPack module <CPack>` which will use these
variables and some other properties of the current system to setup an
installer.

The next step is to build the project in the usual manner and then run the
:manual:`cpack <cpack(1)>` executable. To build a binary distribution, from the
binary directory run:

.. code-block:: console

  cpack

To specify the generator, use the ``-G`` option. For multi-config builds, use
``-C`` to specify the configuration. For example:

.. code-block:: console

  cpack -G ZIP -C Debug

To create a source distribution you would type:

.. code-block:: console

  cpack --config CPackSourceConfig.cmake

Alternatively, run ``make package`` or right click the ``Package`` target and
``Build Project`` from an IDE.

Run the installer found in the binary directory. Then run the installed
executable and verify that it works.

Adding Support for a Dashboard (Step 8)
=======================================

Adding support for submitting our test results to a dashboard is simple. We
already defined a number of tests for our project in `Testing Support`_. Now we
just have to run those tests and submit them to a dashboard. To include support
for dashboards we include the :module:`CTest` module in our top-level
``CMakeLists.txt``.

Replace:

.. code-block:: cmake

  # enable testing
  enable_testing()

With:

.. code-block:: cmake

  # enable dashboard scripting
  include(CTest)

The :module:`CTest` module will automatically call ``enable_testing()``, so we
can remove it from our CMake files.

We will also need to create a ``CTestConfig.cmake`` file in the top-level
directory where we can specify the name of the project and where to submit the
dashboard.

.. literalinclude:: Step9/CTestConfig.cmake
  :language: cmake

The :manual:`ctest <ctest(1)>` executable will read in this file when it runs.
To create a simple dashboard you can run the :manual:`cmake <cmake(1)>`
executable or the :manual:`cmake-gui <cmake-gui(1)>` to configure the project,
but do not build it yet. Instead, change directory to the binary tree, and then
run:

  ctest [-VV] -D Experimental

Remember, for multi-config generators (e.g. Visual Studio), the configuration
type must be specified::

  ctest [-VV] -C Debug -D Experimental

Or, from an IDE, build the ``Experimental`` target.

The :manual:`ctest <ctest(1)>` executable will build and test the project and
submit the results to Kitware's public dashboard:
https://my.cdash.org/index.php?project=CMakeTutorial.

Mixing Static and Shared (Step 9)
=================================

In this section we will show how the :variable:`BUILD_SHARED_LIBS` variable can
be used to control the default behavior of :command:`add_library`,
and allow control over how libraries without an explicit type (``STATIC``,
``SHARED``, ``MODULE`` or ``OBJECT``) are built.

To accomplish this we need to add :variable:`BUILD_SHARED_LIBS` to the
top-level ``CMakeLists.txt``. We use the :command:`option` command as it allows
users to optionally select if the value should be ON or OFF.

Next we are going to refactor MathFunctions to become a real library that
encapsulates using ``mysqrt`` or ``sqrt``, instead of requiring the calling
code to do this logic. This will also mean that ``USE_MYMATH`` will not control
building MathFunctions, but instead will control the behavior of this library.

The first step is to update the starting section of the top-level
``CMakeLists.txt`` to look like:

.. literalinclude:: Step10/CMakeLists.txt
  :language: cmake
  :end-before: # add the binary tree

Now that we have made MathFunctions always be used, we will need to update
the logic of that library. So, in ``MathFunctions/CMakeLists.txt`` we need to
create a SqrtLibrary that will conditionally be built and installed when
``USE_MYMATH`` is enabled. Now, since this is a tutorial, we are going to
explicitly require that SqrtLibrary is built statically.

The end result is that ``MathFunctions/CMakeLists.txt`` should look like:

.. literalinclude:: Step10/MathFunctions/CMakeLists.txt
  :language: cmake
  :lines: 1-36,42-

Next, update ``MathFunctions/mysqrt.cxx`` to use the ``mathfunctions`` and
``detail`` namespaces:

.. literalinclude:: Step10/MathFunctions/mysqrt.cxx
  :language: c++

We also need to make some changes in ``tutorial.cxx``, so that it no longer
uses ``USE_MYMATH``:

#. Always include ``MathFunctions.h``
#. Always use ``mathfunctions::sqrt``
#. Don't include cmath

Finally, update ``MathFunctions/MathFunctions.h`` to use dll export defines:

.. literalinclude:: Step10/MathFunctions/MathFunctions.h
  :language: c++

At this point, if you build everything, you may notice that linking fails
as we are combining a static library without position independent code with a
library that has position independent code. The solution to this is to
explicitly set the :prop_tgt:`POSITION_INDEPENDENT_CODE` target property of
SqrtLibrary to be True no matter the build type.

.. literalinclude:: Step10/MathFunctions/CMakeLists.txt
  :language: cmake
  :lines: 37-42

**Exercise**: We modified ``MathFunctions.h`` to use dll export defines.
Using CMake documentation can you find a helper module to simplify this?


Adding Generator Expressions (Step 10)
======================================

:manual:`Generator expressions <cmake-generator-expressions(7)>` are evaluated
during build system generation to produce information specific to each build
configuration.

:manual:`Generator expressions <cmake-generator-expressions(7)>` are allowed in
the context of many target properties, such as :prop_tgt:`LINK_LIBRARIES`,
:prop_tgt:`INCLUDE_DIRECTORIES`, :prop_tgt:`COMPILE_DEFINITIONS` and others.
They may also be used when using commands to populate those properties, such as
:command:`target_link_libraries`, :command:`target_include_directories`,
:command:`target_compile_definitions` and others.

:manual:`Generator expressions <cmake-generator-expressions(7)>`  may be used
to enable conditional linking, conditional definitions used when compiling,
conditional include directories and more. The conditions may be based on the
build configuration, target properties, platform information or any other
queryable information.

There are different types of
:manual:`generator expressions <cmake-generator-expressions(7)>` including
Logical, Informational, and Output expressions.

Logical expressions are used to create conditional output. The basic
expressions are the 0 and 1 expressions. A ``$<0:...>`` results in the empty
string, and ``<1:...>`` results in the content of "...".  They can also be
nested.

A common usage of
:manual:`generator expressions <cmake-generator-expressions(7)>` is to
conditionally add compiler flags, such as those for language levels or
warnings. A nice pattern is to associate this information to an ``INTERFACE``
target allowing this information to propagate. Let's start by constructing an
``INTERFACE`` target and specifying the required C++ standard level of ``11``
instead of using :variable:`CMAKE_CXX_STANDARD`.

So the following code:

.. literalinclude:: Step10/CMakeLists.txt
  :language: cmake
  :start-after: project(Tutorial VERSION 1.0)
  :end-before: # control where the static and shared libraries are built so that on windows

Would be replaced with:

.. literalinclude:: Step11/CMakeLists.txt
  :language: cmake
  :start-after: project(Tutorial VERSION 1.0)
  :end-before: # add compiler warning flags just when building this project via


Next we add the desired compiler warning flags that we want for our project. As
warning flags vary based on the compiler we use the ``COMPILE_LANG_AND_ID``
generator expression to control which flags to apply given a language and a set
of compiler ids as seen below:

.. literalinclude:: Step11/CMakeLists.txt
  :language: cmake
  :start-after: # the BUILD_INTERFACE genex
  :end-before: # control where the static and shared libraries are built so that on windows

Looking at this we see that the warning flags are encapsulated inside a
``BUILD_INTERFACE`` condition. This is done so that consumers of our installed
project will not inherit our warning flags.


**Exercise**: Modify ``MathFunctions/CMakeLists.txt`` so that all targets have
a :command:`target_link_libraries` call to ``tutorial_compiler_flags``.


Adding Export Configuration (Step 11)
=====================================

During `Installing and Testing (Step 4)`_ of the tutorial we added the ability
for CMake to install the library and headers of the project. During
`Building an Installer (Step 7)`_ we added the ability to package up this
information so it could be distributed to other people.

The next step is to add the necessary information so that other CMake projects
can use our project, be it from a build directory, a local install or when
packaged.

The first step is to update our :command:`install(TARGETS)` commands to not
only specify a ``DESTINATION`` but also an ``EXPORT``. The ``EXPORT`` keyword
generates and installs a CMake file containing code to import all targets
listed in the install command from the installation tree. So let's go ahead and
explicitly ``EXPORT`` the MathFunctions library by updating the ``install``
command in ``MathFunctions/CMakeLists.txt`` to look like:

.. literalinclude:: Complete/MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # install rules

Now that we have MathFunctions being exported, we also need to explicitly
install the generated ``MathFunctionsTargets.cmake`` file. This is done by
adding the following to the bottom of the top-level ``CMakeLists.txt``:

.. literalinclude:: Complete/CMakeLists.txt
  :language: cmake
  :start-after: # install the configuration targets
  :end-before: include(CMakePackageConfigHelpers)

At this point you should try and run CMake. If everything is setup properly
you will see that CMake will generate an error that looks like:

.. code-block:: console

  Target "MathFunctions" INTERFACE_INCLUDE_DIRECTORIES property contains
  path:

    "/Users/robert/Documents/CMakeClass/Tutorial/Step11/MathFunctions"

  which is prefixed in the source directory.

What CMake is trying to say is that during generating the export information
it will export a path that is intrinsically tied to the current machine and
will not be valid on other machines. The solution to this is to update the
MathFunctions :command:`target_include_directories` to understand that it needs
different ``INTERFACE`` locations when being used from within the build
directory and from an install / package. This means converting the
:command:`target_include_directories` call for MathFunctions to look like:

.. literalinclude:: Step12/MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # to find MathFunctions.h, while we don't.
  :end-before: # should we use our own math functions

Once this has been updated, we can re-run CMake and verify that it doesn't
warn anymore.

At this point, we have CMake properly packaging the target information that is
required but we will still need to generate a ``MathFunctionsConfig.cmake`` so
that the CMake :command:`find_package` command can find our project. So let's go
ahead and add a new file to the top-level of the project called
``Config.cmake.in`` with the following contents:

.. literalinclude:: Step12/Config.cmake.in

Then, to properly configure and install that file, add the following to the
bottom of the top-level ``CMakeLists.txt``:

.. literalinclude:: Step12/CMakeLists.txt
  :language: cmake
  :start-after: # install the configuration targets
  :end-before: # generate the export

At this point, we have generated a relocatable CMake Configuration for our
project that can be used after the project has been installed or packaged. If
we want our project to also be used from a build directory we only have to add
the following to the bottom of the top level ``CMakeLists.txt``:

.. literalinclude:: Step12/CMakeLists.txt
  :language: cmake
  :start-after: # needs to be after the install(TARGETS ) command

With this export call we now generate a ``Targets.cmake``, allowing the
configured ``MathFunctionsConfig.cmake`` in the build directory to be used by
other projects, without needing it to be installed.

Packaging Debug and Release (Step 12)
=====================================

**Note:** This example is valid for single-configuration generators and will
not work for multi-configuration generators (e.g. Visual Studio).

By default, CMake's model is that a build directory only contains a single
configuration, be it Debug, Release, MinSizeRel, or RelWithDebInfo. It is
possible, however, to setup CPack to bundle multiple build directories and
construct a package that contains multiple configurations of the same project.

First, we want to ensure that the debug and release builds use different names
for the executables and libraries that will be installed. Let's use `d` as the
postfix for the debug executable and libraries.

Set :variable:`CMAKE_DEBUG_POSTFIX` near the beginning of the top-level
``CMakeLists.txt`` file:

.. literalinclude:: Complete/CMakeLists.txt
  :language: cmake
  :start-after: project(Tutorial VERSION 1.0)
  :end-before: target_compile_features(tutorial_compiler_flags

And the :prop_tgt:`DEBUG_POSTFIX` property on the tutorial executable:

.. literalinclude:: Complete/CMakeLists.txt
  :language: cmake
  :start-after: # add the executable
  :end-before: # add the binary tree to the search path for include files

Let's also add version numbering to the MathFunctions library. In
``MathFunctions/CMakeLists.txt``, set the :prop_tgt:`VERSION` and
:prop_tgt:`SOVERSION` properties:

.. literalinclude:: Complete/MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # setup the version numbering
  :end-before: # install rules

From the ``Step12`` directory, create ``debug`` and ``release``
subbdirectories. The layout will look like:

.. code-block:: none

  - Step12
     - debug
     - release

Now we need to setup debug and release builds. We can use
:variable:`CMAKE_BUILD_TYPE` to set the configuration type:

.. code-block:: console

  cd debug
  cmake -DCMAKE_BUILD_TYPE=Debug ..
  cmake --build .
  cd ../release
  cmake -DCMAKE_BUILD_TYPE=Release ..
  cmake --build .

Now that both the debug and release builds are complete, we can use a custom
configuration file to package both builds into a single release. In the
``Step12`` directory, create a file called ``MultiCPackConfig.cmake``. In this
file, first include the default configuration file that was created by the
:manual:`cmake  <cmake(1)>` executable.

Next, use the ``CPACK_INSTALL_CMAKE_PROJECTS`` variable to specify which
projects to install. In this case, we want to install both debug and release.

.. literalinclude:: Complete/MultiCPackConfig.cmake
  :language: cmake

From the ``Step12`` directory, run :manual:`cpack <cpack(1)>` specifying our
custom configuration file with the ``config`` option:

.. code-block:: console

  cpack --config MultiCPackConfig.cmake
