.. cmake-manual-description: CMake Server

cmake-server(7)
***************

.. only:: html

   .. contents::

.. deprecated:: 3.15

  This will be removed from a future version of CMake.
  Clients should use the :manual:`cmake-file-api(7)` instead.

Introduction
============

:manual:`cmake(1)` is capable of providing semantic information about
CMake code it executes to generate a buildsystem.  If executed with
the ``-E server`` command line options, it starts in a long running mode
and allows a client to request the available information via a JSON protocol.

The protocol is designed to be useful to IDEs, refactoring tools, and
other tools which have a need to understand the buildsystem in entirety.

A single :manual:`cmake-buildsystem(7)` may describe buildsystem contents
and build properties which differ based on
:manual:`generation-time context <cmake-generator-expressions(7)>`
including:

* The Platform (eg, Windows, APPLE, Linux).
* The build configuration (eg, Debug, Release, Coverage).
* The Compiler (eg, MSVC, GCC, Clang) and compiler version.
* The language of the source files compiled.
* Available compile features (eg CXX variadic templates).
* CMake policies.

The protocol aims to provide information to tooling to satisfy several
needs:

#. Provide a complete and easily parsed source of all information relevant
   to the tooling as it relates to the source code.  There should be no need
   for tooling to parse generated buildsystems to access include directories
   or compile definitions for example.
#. Semantic information about the CMake buildsystem itself.
#. Provide a stable interface for reading the information in the CMake cache.
#. Information for determining when cmake needs to be re-run as a result of
   file changes.


Operation
=========

Start :manual:`cmake(1)` in the server command mode, supplying the path to
the build directory to process::

  cmake -E server (--debug|--pipe=<NAMED_PIPE>)

The server will communicate using stdin/stdout (with the ``--debug`` parameter)
or using a named pipe (with the ``--pipe=<NAMED_PIPE>`` parameter).  Note
that "named pipe" refers to a local domain socket on Unix and to a named pipe
on Windows.

When connecting to the server (via named pipe or by starting it in ``--debug``
mode), the server will reply with a hello message::

  [== "CMake Server" ==[
  {"supportedProtocolVersions":[{"major":1,"minor":0}],"type":"hello"}
  ]== "CMake Server" ==]

Messages sent to and from the process are wrapped in magic strings::

  [== "CMake Server" ==[
  {
    ... some JSON message ...
  }
  ]== "CMake Server" ==]

The server is now ready to accept further requests via the named pipe
or stdin.


Debugging
=========

CMake server mode can be asked to provide statistics on execution times, etc.
or to dump a copy of the response into a file. This is done passing a "debug"
JSON object as a child of the request.

The debug object supports the "showStats" key, which takes a boolean and makes
the server mode return a "zzzDebug" object with stats as part of its response.
"dumpToFile" takes a string value and will cause the cmake server to copy
the response into the given filename.

This is a response from the cmake server with "showStats" set to true::

  [== "CMake Server" ==[
  {
    "cookie":"",
    "errorMessage":"Waiting for type \"handshake\".",
    "inReplyTo":"unknown",
   "type":"error",
    "zzzDebug": {
      "dumpFile":"/tmp/error.txt",
      "jsonSerialization":0.011016,
      "size":111,
      "totalTime":0.025995
    }
  }
  ]== "CMake Server" ==]

The server has made a copy of this response into the file /tmp/error.txt and
took 0.011 seconds to turn the JSON response into a string, and it took 0.025
seconds to process the request in total. The reply has a size of 111 bytes.


Protocol API
============


General Message Layout
----------------------

All messages need to have a "type" value, which identifies the type of
message that is passed back or forth. E.g. the initial message sent by the
server is of type "hello". Messages without a type will generate an response
of type "error".

All requests sent to the server may contain a "cookie" value. This value
will he handed back unchanged in all responses triggered by the request.

All responses will contain a value "inReplyTo", which may be empty in
case of parse errors, but will contain the type of the request message
in all other cases.


Type "reply"
^^^^^^^^^^^^

This type is used by the server to reply to requests.

The message may -- depending on the type of the original request --
contain values.

Example::

  [== "CMake Server" ==[
  {"cookie":"zimtstern","inReplyTo":"handshake","type":"reply"}
  ]== "CMake Server" ==]


Type "error"
^^^^^^^^^^^^

This type is used to return an error condition to the client. It will
contain an "errorMessage".

Example::

  [== "CMake Server" ==[
  {"cookie":"","errorMessage":"Protocol version not supported.","inReplyTo":"handshake","type":"error"}
  ]== "CMake Server" ==]


Type "progress"
^^^^^^^^^^^^^^^

When the server is busy for a long time, it is polite to send back replies of
type "progress" to the client. These will contain a "progressMessage" with a
string describing the action currently taking place as well as
"progressMinimum", "progressMaximum" and "progressCurrent" with integer values
describing the range of progress.

Messages of type "progress" will be followed by more "progress" messages or with
a message of type "reply" or "error" that complete the request.

"progress" messages may not be emitted after the "reply" or "error" message for
the request that triggered the responses was delivered.


Type "message"
^^^^^^^^^^^^^^

A message is triggered when the server processes a request and produces some
form of output that should be displayed to the user. A Message has a "message"
with the actual text to display as well as a "title" with a suggested dialog
box title.

Example::

  [== "CMake Server" ==[
  {"cookie":"","message":"Something happened.","title":"Title Text","inReplyTo":"handshake","type":"message"}
  ]== "CMake Server" ==]


Type "signal"
^^^^^^^^^^^^^

The server can send signals when it detects changes in the system state. Signals
are of type "signal", have an empty "cookie" and "inReplyTo" field and always
have a "name" set to show which signal was sent.


Specific Signals
----------------

The cmake server may sent signals with the following names:

"dirty" Signal
^^^^^^^^^^^^^^

The "dirty" signal is sent whenever the server determines that the configuration
of the project is no longer up-to-date. This happens when any of the files that have
an influence on the build system is changed.

The "dirty" signal may look like this::

  [== "CMake Server" ==[
  {
    "cookie":"",
    "inReplyTo":"",
    "name":"dirty",
    "type":"signal"}
  ]== "CMake Server" ==]


"fileChange" Signal
^^^^^^^^^^^^^^^^^^^

The "fileChange" signal is sent whenever a watched file is changed. It contains
the "path" that has changed and a list of "properties" with the kind of change
that was detected. Possible changes are "change" and "rename".

The "fileChange" signal looks like this::

  [== "CMake Server" ==[
  {
    "cookie":"",
    "inReplyTo":"",
    "name":"fileChange",
    "path":"/absolute/CMakeLists.txt",
    "properties":["change"],
    "type":"signal"}
  ]== "CMake Server" ==]


Specific Message Types
----------------------


Type "hello"
^^^^^^^^^^^^

The initial message send by the cmake server on startup is of type "hello".
This is the only message ever sent by the server that is not of type "reply",
"progress" or "error".

It will contain "supportedProtocolVersions" with an array of server protocol
versions supported by the cmake server. These are JSON objects with "major" and
"minor" keys containing non-negative integer values. Some versions may be marked
as experimental. These will contain the "isExperimental" key set to true. Enabling
these requires a special command line argument when starting the cmake server mode.

Within a "major" version all "minor" versions are fully backwards compatible.
New "minor" versions may introduce functionality in such a way that existing
clients of the same "major" version will continue to work, provided they
ignore keys in the output that they do not know about.

Example::

  [== "CMake Server" ==[
  {"supportedProtocolVersions":[{"major":0,"minor":1}],"type":"hello"}
  ]== "CMake Server" ==]


Type "handshake"
^^^^^^^^^^^^^^^^

The first request that the client may send to the server is of type "handshake".

This request needs to pass one of the "supportedProtocolVersions" of the "hello"
type response received earlier back to the server in the "protocolVersion" field.
Giving the "major" version of the requested protocol version will make the server
use the latest minor version of that protocol. Use this if you do not explicitly
need to depend on a specific minor version.

Protocol version 1.0 requires the following attributes to be set:

  * "sourceDirectory" with a path to the sources
  * "buildDirectory" with a path to the build directory
  * "generator" with the generator name
  * "extraGenerator" (optional!) with the extra generator to be used
  * "platform" with the generator platform (if supported by the generator)
  * "toolset" with the generator toolset (if supported by the generator)

Protocol version 1.2 makes all but the build directory optional, provided
there is a valid cache in the build directory that contains all the other
information already.

Example::

  [== "CMake Server" ==[
  {"cookie":"zimtstern","type":"handshake","protocolVersion":{"major":0},
   "sourceDirectory":"/home/code/cmake", "buildDirectory":"/tmp/testbuild",
   "generator":"Ninja"}
  ]== "CMake Server" ==]

which will result in a response type "reply"::

  [== "CMake Server" ==[
  {"cookie":"zimtstern","inReplyTo":"handshake","type":"reply"}
  ]== "CMake Server" ==]

indicating that the server is ready for action.


Type "globalSettings"
^^^^^^^^^^^^^^^^^^^^^

This request can be sent after the initial handshake. It will return a
JSON structure with information on cmake state.

Example::

  [== "CMake Server" ==[
  {"type":"globalSettings"}
  ]== "CMake Server" ==]

which will result in a response type "reply"::

  [== "CMake Server" ==[
  {
    "buildDirectory": "/tmp/test-build",
    "capabilities": {
      "generators": [
        {
          "extraGenerators": [],
          "name": "Watcom WMake",
          "platformSupport": false,
          "toolsetSupport": false
        },
        <...>
      ],
      "serverMode": false,
      "version": {
        "isDirty": false,
        "major": 3,
        "minor": 6,
        "patch": 20160830,
        "string": "3.6.20160830-gd6abad",
        "suffix": "gd6abad"
      }
    },
    "checkSystemVars": false,
    "cookie": "",
    "extraGenerator": "",
    "generator": "Ninja",
    "debugOutput": false,
    "inReplyTo": "globalSettings",
    "sourceDirectory": "/home/code/cmake",
    "trace": false,
    "traceExpand": false,
    "type": "reply",
    "warnUninitialized": false,
    "warnUnused": false,
    "warnUnusedCli": true
  }
  ]== "CMake Server" ==]


Type "setGlobalSettings"
^^^^^^^^^^^^^^^^^^^^^^^^

This request can be sent to change the global settings attributes. Unknown
attributes are going to be ignored. Read-only attributes reported by
"globalSettings" are all capabilities, buildDirectory, generator,
extraGenerator and sourceDirectory. Any attempt to set these will be ignored,
too.

All other settings will be changed.

The server will respond with an empty reply message or an error.

Example::

  [== "CMake Server" ==[
  {"type":"setGlobalSettings","debugOutput":true}
  ]== "CMake Server" ==]

CMake will reply to this with::

  [== "CMake Server" ==[
  {"inReplyTo":"setGlobalSettings","type":"reply"}
  ]== "CMake Server" ==]


Type "configure"
^^^^^^^^^^^^^^^^

This request will configure a project for build.

To configure a build directory already containing cmake files, it is enough to
set "buildDirectory" via "setGlobalSettings". To create a fresh build directory
you also need to set "currentGenerator" and "sourceDirectory" via "setGlobalSettings"
in addition to "buildDirectory".

You may a list of strings to "configure" via the "cacheArguments" key. These
strings will be interpreted similar to command line arguments related to
cache handling that are passed to the cmake command line client.

Example::

  [== "CMake Server" ==[
  {"type":"configure", "cacheArguments":["-Dsomething=else"]}
  ]== "CMake Server" ==]

CMake will reply like this (after reporting progress for some time)::

  [== "CMake Server" ==[
  {"cookie":"","inReplyTo":"configure","type":"reply"}
  ]== "CMake Server" ==]


Type "compute"
^^^^^^^^^^^^^^

This request will generate build system files in the build directory and
is only available after a project was successfully "configure"d.

Example::

  [== "CMake Server" ==[
  {"type":"compute"}
  ]== "CMake Server" ==]

CMake will reply (after reporting progress information)::

  [== "CMake Server" ==[
  {"cookie":"","inReplyTo":"compute","type":"reply"}
  ]== "CMake Server" ==]


Type "codemodel"
^^^^^^^^^^^^^^^^

The "codemodel" request can be used after a project was "compute"d successfully.

It will list the complete project structure as it is known to cmake.

The reply will contain a key "configurations", which will contain a list of
configuration objects. Configuration objects are used to destinquish between
different configurations the build directory might have enabled. While most
generators only support one configuration, others might support several.

Each configuration object can have the following keys:

"name"
  contains the name of the configuration. The name may be empty.
"projects"
  contains a list of project objects, one for each build project.

Project objects define one (sub-)project defined in the cmake build system.

Each project object can have the following keys:

"name"
  contains the (sub-)projects name.
"minimumCMakeVersion"
  contains the minimum cmake version allowed for this project, null if the
  project doesn't specify one.
"hasInstallRule"
  true if the project contains any install rules, false otherwise.
"sourceDirectory"
  contains the current source directory
"buildDirectory"
  contains the current build directory.
"targets"
  contains a list of build system target objects.

Target objects define individual build targets for a certain configuration.

Each target object can have the following keys:

"name"
  contains the name of the target.
"type"
  defines the type of build of the target. Possible values are
  "STATIC_LIBRARY", "MODULE_LIBRARY", "SHARED_LIBRARY", "OBJECT_LIBRARY",
  "EXECUTABLE", "UTILITY" and "INTERFACE_LIBRARY".
"fullName"
  contains the full name of the build result (incl. extensions, etc.).
"sourceDirectory"
  contains the current source directory.
"buildDirectory"
  contains the current build directory.
"isGeneratorProvided"
  true if the target is auto-created by a generator, false otherwise
"hasInstallRule"
  true if the target contains any install rules, false otherwise.
"installPaths"
  full path to the destination directories defined by target install rules.
"artifacts"
  with a list of build artifacts. The list is sorted with the most
  important artifacts first (e.g. a .DLL file is listed before a
  .PDB file on windows).
"linkerLanguage"
  contains the language of the linker used to produce the artifact.
"linkLibraries"
  with a list of libraries to link to. This value is encoded in the
  system's native shell format.
"linkFlags"
  with a list of flags to pass to the linker. This value is encoded in
  the system's native shell format.
"linkLanguageFlags"
  with the flags for a compiler using the linkerLanguage. This value is
  encoded in the system's native shell format.
"frameworkPath"
  with the framework path (on Apple computers). This value is encoded
  in the system's native shell format.
"linkPath"
  with the link path. This value is encoded in the system's native shell
  format.
"sysroot"
  with the sysroot path.
"fileGroups"
  contains the source files making up the target.

FileGroups are used to group sources using similar settings together.

Each fileGroup object may contain the following keys:

"language"
  contains the programming language used by all files in the group.
"compileFlags"
  with a string containing all the flags passed to the compiler
  when building any of the files in this group. This value is encoded in
  the system's native shell format.
"includePath"
  with a list of include paths. Each include path is an object
  containing a "path" with the actual include path and "isSystem" with a bool
  value informing whether this is a normal include or a system include. This
  value is encoded in the system's native shell format.
"defines"
  with a list of defines in the form "SOMEVALUE" or "SOMEVALUE=42". This
  value is encoded in the system's native shell format.
"sources"
  with a list of source files.

All file paths in the fileGroup are either absolute or relative to the
sourceDirectory of the target.

Example::

  [== "CMake Server" ==[
  {"type":"codemodel"}
  ]== "CMake Server" ==]

CMake will reply::

  [== "CMake Server" ==[
  {
    "configurations": [
      {
        "name": "",
        "projects": [
          {
            "buildDirectory": "/tmp/build/Source/CursesDialog/form",
            "name": "CMAKE_FORM",
            "sourceDirectory": "/home/code/src/cmake/Source/CursesDialog/form",
            "targets": [
              {
                "artifacts": [ "/tmp/build/Source/CursesDialog/form/libcmForm.a" ],
                "buildDirectory": "/tmp/build/Source/CursesDialog/form",
                "fileGroups": [
                  {
                    "compileFlags": "  -std=gnu11",
                    "defines": [ "CURL_STATICLIB", "LIBARCHIVE_STATIC" ],
                    "includePath": [ { "path": "/tmp/build/Utilities" }, <...> ],
                    "isGenerated": false,
                    "language": "C",
                    "sources": [ "fld_arg.c", <...> ]
                  }
                ],
                "fullName": "libcmForm.a",
                "linkerLanguage": "C",
                "name": "cmForm",
                "sourceDirectory": "/home/code/src/cmake/Source/CursesDialog/form",
                "type": "STATIC_LIBRARY"
              }
            ]
          },
          <...>
        ]
      }
    ],
    "cookie": "",
    "inReplyTo": "codemodel",
    "type": "reply"
  }
  ]== "CMake Server" ==]


Type "ctestInfo"
^^^^^^^^^^^^^^^^

The "ctestInfo" request can be used after a project was "compute"d successfully.

It will list the complete project test structure as it is known to cmake.

The reply will contain a key "configurations", which will contain a list of
configuration objects. Configuration objects are used to destinquish between
different configurations the build directory might have enabled. While most
generators only support one configuration, others might support several.

Each configuration object can have the following keys:

"name"
  contains the name of the configuration. The name may be empty.
"projects"
  contains a list of project objects, one for each build project.

Project objects define one (sub-)project defined in the cmake build system.

Each project object can have the following keys:

"name"
  contains the (sub-)projects name.
"ctestInfo"
  contains a list of test objects.

Each test object can have the following keys:

"ctestName"
  contains the name of the test.
"ctestCommand"
  contains the test command.
"properties"
  contains a list of test property objects.

Each test property object can have the following keys:

"key"
  contains the test property key.
"value"
  contains the test property value.


Type "cmakeInputs"
^^^^^^^^^^^^^^^^^^

The "cmakeInputs" requests will report files used by CMake as part
of the build system itself.

This request is only available after a project was successfully
"configure"d.

Example::

  [== "CMake Server" ==[
  {"type":"cmakeInputs"}
  ]== "CMake Server" ==]

CMake will reply with the following information::

  [== "CMake Server" ==[
  {"buildFiles":
    [
      {"isCMake":true,"isTemporary":false,"sources":["/usr/lib/cmake/...", ... ]},
      {"isCMake":false,"isTemporary":false,"sources":["CMakeLists.txt", ...]},
      {"isCMake":false,"isTemporary":true,"sources":["/tmp/build/CMakeFiles/...", ...]}
    ],
    "cmakeRootDirectory":"/usr/lib/cmake",
    "sourceDirectory":"/home/code/src/cmake",
    "cookie":"",
    "inReplyTo":"cmakeInputs",
    "type":"reply"
  }
  ]== "CMake Server" ==]

All file names are either relative to the top level source directory or
absolute.

The list of files which "isCMake" set to true are part of the cmake installation.

The list of files witch "isTemporary" set to true are part of the build directory
and will not survive the build directory getting cleaned out.


Type "cache"
^^^^^^^^^^^^

The "cache" request will list the cached configuration values.

Example::

  [== "CMake Server" ==[
  {"type":"cache"}
  ]== "CMake Server" ==]

CMake will respond with the following output::

  [== "CMake Server" ==[
  {
    "cookie":"","inReplyTo":"cache","type":"reply",
    "cache":
    [
      {
        "key":"SOMEVALUE",
        "properties":
        {
          "ADVANCED":"1",
          "HELPSTRING":"This is not helpful"
        }
        "type":"STRING",
        "value":"TEST"}
    ]
  }
  ]== "CMake Server" ==]

The output can be limited to a list of keys by passing an array of key names
to the "keys" optional field of the "cache" request.


Type "fileSystemWatchers"
^^^^^^^^^^^^^^^^^^^^^^^^^

The server can watch the filesystem for changes. The "fileSystemWatchers"
command will report on the files and directories watched.

Example::

  [== "CMake Server" ==[
  {"type":"fileSystemWatchers"}
  ]== "CMake Server" ==]

CMake will respond with the following output::

  [== "CMake Server" ==[
  {
    "cookie":"","inReplyTo":"fileSystemWatchers","type":"reply",
    "watchedFiles": [ "/absolute/path" ],
    "watchedDirectories": [ "/absolute" ]
  }
  ]== "CMake Server" ==]
