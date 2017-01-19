# How to contribute

We definitely welcome patches and contribution to grpc! Here is some guideline
and information about how to do so.

## Getting started

### Legal requirements

In order to protect both you and ourselves, you will need to sign the
[Contributor License Agreement](https://cla.developers.google.com/clas).

### Technical requirements

Make sure you have followed all of the steps in [INSTALL](INSTALL.md).

## Testing your changes

We provide a tool to help run the suite of tests in various environments.
In order to run most of the available tests, one would need to run:

`./tools/run_tests/run_tests.py`

If you want to run tests for any of the languages {c, c++, csharp, node, objc, php, python, ruby}, do this:

`./tools/run_tests/run_tests.py -l <lang>`

To know about the list of available commands, do this:

`./tools/run_tests/run_tests.py -h`

If you are running tests for ObjC on osx, follow these steps before running tests:
* install Xcode command-line tools by running
`sudo xcode-select --install`
* install macports from https://www.macports.org/install.php
* install autoconf, automake, libtool, gflags, cmake using macports
* restart your terminal window or run source ~/.bash_profile to pick up the new PATH changes.

## Adding or removing source code

Each language uses its own build system to work. Currently, the root's Makefile
and the Visual Studio project files are building only the C and C++ source code.
In order to ease the maintenance of these files, we have a
template system. Please do not contribute manual changes to any of the generated
files. Instead, modify the template files, or the build.yaml file, and
re-generate the project files using the following command:

`./tools/buildgen/generate_projects.sh`

You'll find more information about this in the [templates](templates) folder.
