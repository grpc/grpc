# Regenerating project files

Prerequisites: `python`, `pip install mako`

```
# Regenerate the projects files using templates
tools/buildgen/generate_projects.sh
```

# Quick justification

We've approached the problem of the build system from a lot of different
angles. The main issue was that there isn't a single build system that
was going to single handedly cover all of our usage cases.

So instead we decided to work the following way:

* A `build.yaml` file at the root is the source of truth for listing all the
targets and files needed to build grpc and its tests, as well as a basic system
for dependency description.

* Each project file (Makefile, Visual Studio project files, Bazel's BUILD) is
a [YAML](http://yaml.org) file used by the `build.yaml` file to generate the
final output file.

This way we can maintain as many project system as we see fit, without having
to manually maintain them when we add or remove new code to the repository.
Only the structure of the project file is relevant to the template. The actual
list of source code and targets isn't.

We currently have template files for GNU Make, Visual Studio 2013,
[Bazel](http://bazel.io) and [gyp](https://gyp.gsrc.io/) (albeit only for
Node.js). In the future, we
would like to expand to also generate [cmake](https://cmake.org)
project files, XCode project files, and an Android.mk file allowing to compile
gRPC using Android's NDK.

We'll gladly accept contribution that'd create additional project files
using that system.

# Structure of `build.yaml`

The `build.yaml` file has the following structure:

```
settings:  # global settings, such as version number
  ...
filegroups:  # groups of files that are automatically expanded
  ...
libs:  # list of libraries to build
  ...
target:   # list of targets to build
  ...
```

The `filegroups` are helpful to re-use a subset of files in multiple targets.
One `filegroups` entry has the following structure:

```
- name: "arbitrary string", # the name of the filegroup
  public_headers: # list of public headers defined in that filegroup
  - ...
  headers: # list of headers defined in that filegroup
  - ...
  src: # list of source files defined in that filegroup
  - ...
```

The `libs` collection contains the list of all the libraries we describe. Some may be
helper libraries for the tests. Some may be installable libraries. Some may be
helper libraries for installable binaries.

The `targets` array contains the list of all the binary targets we describe. Some may
be installable binaries.

One `libs` or `targets` entry has the following structure (see below for
details):

```
name: "arbitrary string", # the name of the library
build: "build type",      # in which situation we want that library to be
                          # built and potentially installed (see below).
language: "...",          # the language tag; "c" or "c++"
public_headers:           # list of public headers to install
headers:                  # list of headers used by that target
src:                      # list of files to compile
secure: boolean,          # see below
baselib: boolean,         # this is a low level library that has system
                          # dependencies
vs_project_guid: '{...}', # Visual Studio's unique guid for that project
filegroups:               # list of filegroups to merge to that project
                          # note that this will be expanded automatically
deps:                     # list of libraries this target depends on
deps_linkage: "..."       # "static"  or "dynamic". Used by the Makefile only to
                          # determine the way dependencies are linkned. Defaults
                          # to "dynamic".
dll: "..."                # see below.
dll_def: "..."            # Visual Studio's dll definition file.
vs_props:                 # List of property sheets to attach to that project.
vs_config_type: "..."     # DynamicLibrary/StaticLibrary. Used only when
                          # creating a library. Specifies if we're building a
                          # static library or a dll. Use in conjunction with `dll_def`.
vs_packages:              # List of nuget packages this project depends on.
```

## The `"build"` tag

Currently, the "`build`" tag have these meanings:

* `"all"`: library to build on `"make all"`, and install on the system.
* `"protoc"`: a protoc plugin to build on `"make all"` and install on the system.
* `"private"`: a library to only build for tests.
* `"test"`: a test binary to run on `"make test"`.
* `"tool"`: a binary to be built upon `"make tools"`.

All of the targets should always be present in the generated project file, if
possible and applicable. But the build tag is what should group the targets
together in a single build command.


## The `"secure"` tag

This means this target requires OpenSSL one way or another. The values can be
`"yes"`, `"no"` and `"check"`. The default value is `"check"`. It means that
the target requires OpenSSL, but that since the target depends on another one
that is supposed to also import OpenSSL, the import should then be implicitely
transitive. `"check"` should then only disable that target if OpenSSL hasn't
been found or is unavailable.

## The `"baselib"` boolean

This means this is a library that will provide most of the features for gRPC.
In particular, if we're locally building OpenSSL, protobuf or zlib, then we
should merge OpenSSL, protobuf or zlib inside that library. That effect depends
on the `"language"` tag. OpenSSL and zlib are for `"c"` libraries, while
protobuf is for `"c++"` ones.

## The `"dll"` tag

Used only by Visual Studio's project files. "true" means the project will be
built with both static and dynamic runtimes. "false" means it'll only be built
with static runtime. "only" means it'll only be built with the dll runtime.

## The `"dll_def"` tag

Specifies the visual studio's dll definition file. When creating a DLL, you
sometimes (not always) need a def file (see grpc.def).


# The template system

We're currently using the [mako templates](http://www.makotemplates.org/)
renderer. That choice enables us to simply render text files without dragging
with us a lot of other features. Feel free to explore the current templates
in that directory. The simplest one is probably [BUILD.template](BUILD.template)
which is used to create the [Bazel](http://bazel.io/) project file.

## The renderer engine

As mentioned, the renderer is using [mako templates](http://www.makotemplates.org/),
but some glue is needed to process all of that. See the [buildgen folder](../tools/buildgen)
for more details. We're mainly loading the build.json file, and massaging it,
in order to get the list of properties we need, into a Python dictionary, that
is then passed to the template while rending it.

## The plugins

The file build.json itself isn't passed straight to the template files. It is
first processed and modified by a few plugins. For example, the `filegroups`
expander is [a plugin](../tools/buildgen/plugins/expand_filegroups.py).

The structure of a plugin is simple. The plugin must defined the function
`mako_plugin` that takes a Python dictionary. That dictionary represents the
current state of the build.json contents. The plugin can alter it to whatever
feature it needs to add.
