# How to contribute

We definitely welcome patches and contribution to grpc! Here is some guideline
and information about how to do so.

## Getting started

### Legal requirements

In order to protect both you and ourselves, you will need to sign the
[Contributor License Agreement](https://cla.developers.google.com/clas).

### Technical requirements

You will need several tools to work with this repository. In addition to all of
the packages described in the [INSTALL](INSTALL) file, you will also need
python, and the mako template renderer. To install the latter, using pip, one
should simply be able to do `pip install mako`.

In order to run all of the tests we provide, you will need valgrind and clang.
More specifically, under debian, you will need the package libc++-dev to
properly run all the tests.

If you are planning to work on any of the languages other than C and C++, you
will also need their appropriate development environments.

If you want to work under Windows, we recommend the use of Visual Studio 2013.
The [Community or Express editions](http://www.visualstudio.com/en-us/downloads/download-visual-studio-vs.aspx)
are free and suitable for developing with grpc. Note however that our test
environment and tools are available for Unix environments only at the moment.

## Testing your changes

We provide a tool to help run the suite of tests in various environments.
In order to run most of the available tests, one would need to run:

`./tools/run_tests/run_tests.py`

If you want to run all the possible tests for any of the languages {c, c++, node, php, python}, do this:

`./tools/run_tests/run_tests.py -l <lang> -c all`

## Adding or removing source code

Each language uses its own build system to work. Currently, the root's Makefile
and the Visual Studio project files are building only the C and C++ source code.
In order to ease the maintenance of these files, we have a
template system. Please do not contribute manual changes to any of the generated
files. Instead, modify the template files, or the build.json file, and
re-generate the project files using the following command:

`./tools/buildgen/generate_projects.sh`

