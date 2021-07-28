.. cmake-manual-description: CMake Policies Reference

cmake-policies(7)
*****************

.. only:: html

   .. contents::

Introduction
============

Policies in CMake are used to preserve backward compatible behavior
across multiple releases.  When a new policy is introduced, newer CMake
versions will begin to warn about the backward compatible behavior.  It
is possible to disable the warning by explicitly requesting the OLD, or
backward compatible behavior using the :command:`cmake_policy` command.
It is also possible to request ``NEW``, or non-backward compatible behavior
for a policy, also avoiding the warning.  Each policy can also be set to
either ``NEW`` or ``OLD`` behavior explicitly on the command line with the
:variable:`CMAKE_POLICY_DEFAULT_CMP<NNNN>` variable.

A policy is a deprecation mechanism and not a reliable feature toggle.
A policy should almost never be set to ``OLD``, except to silence warnings
in an otherwise frozen or stable codebase, or temporarily as part of a
larger migration path. The ``OLD`` behavior of each policy is undesirable
and will be replaced with an error condition in a future release.

The :command:`cmake_minimum_required` command does more than report an
error if a too-old version of CMake is used to build a project.  It
also sets all policies introduced in that CMake version or earlier to
``NEW`` behavior.  To manage policies without increasing the minimum required
CMake version, the :command:`if(POLICY)` command may be used:

.. code-block:: cmake

  if(POLICY CMP0990)
    cmake_policy(SET CMP0990 NEW)
  endif()

This has the effect of using the ``NEW`` behavior with newer CMake releases which
users may be using and not issuing a compatibility warning.

The setting of a policy is confined in some cases to not propagate to the
parent scope.  For example, if the files read by the :command:`include` command
or the :command:`find_package` command contain a use of :command:`cmake_policy`,
that policy setting will not affect the caller by default.  Both commands accept
an optional ``NO_POLICY_SCOPE`` keyword to control this behavior.

The :variable:`CMAKE_MINIMUM_REQUIRED_VERSION` variable may also be used
to determine whether to report an error on use of deprecated macros or
functions.

Policies Introduced by CMake 3.19
=================================

.. toctree::
   :maxdepth: 1

   CMP0114: ExternalProject step targets fully adopt their steps. </policy/CMP0114>
   CMP0113: Makefile generators do not repeat custom commands from target dependencies. </policy/CMP0113>
   CMP0112: Target file component generator expressions do not add target dependencies. </policy/CMP0112>
   CMP0111: An imported target missing its location property fails during generation. </policy/CMP0111>
   CMP0110: add_test() supports arbitrary characters in test names. </policy/CMP0110>
   CMP0109: find_program() requires permission to execute but not to read. </policy/CMP0109>

Policies Introduced by CMake 3.18
=================================

.. toctree::
   :maxdepth: 1

   CMP0108: A target cannot link to itself through an alias. </policy/CMP0108>
   CMP0107: An ALIAS target cannot overwrite another target. </policy/CMP0107>
   CMP0106: The Documentation module is removed. </policy/CMP0106>
   CMP0105: Device link step uses the link options. </policy/CMP0105>
   CMP0104: CMAKE_CUDA_ARCHITECTURES now detected for NVCC, empty CUDA_ARCHITECTURES not allowed. </policy/CMP0104>
   CMP0103: Multiple export() with same FILE without APPEND is not allowed. </policy/CMP0103>

Policies Introduced by CMake 3.17
=================================

.. toctree::
   :maxdepth: 1

   CMP0102: mark_as_advanced() does nothing if a cache entry does not exist. </policy/CMP0102>
   CMP0101: target_compile_options honors BEFORE keyword in all scopes. </policy/CMP0101>
   CMP0100: Let AUTOMOC and AUTOUIC process .hh header files. </policy/CMP0100>
   CMP0099: Link properties are transitive over private dependency on static libraries. </policy/CMP0099>
   CMP0098: FindFLEX runs flex in CMAKE_CURRENT_BINARY_DIR when executing. </policy/CMP0098>

Policies Introduced by CMake 3.16
=================================

.. toctree::
   :maxdepth: 1

   CMP0097: ExternalProject_Add with GIT_SUBMODULES "" initializes no submodules. </policy/CMP0097>
   CMP0096: project() preserves leading zeros in version components. </policy/CMP0096>
   CMP0095: RPATH entries are properly escaped in the intermediary CMake install script. </policy/CMP0095>

Policies Introduced by CMake 3.15
=================================

.. toctree::
   :maxdepth: 1

   CMP0094: FindPython3, FindPython2 and FindPython use LOCATION for lookup strategy. </policy/CMP0094>
   CMP0093: FindBoost reports Boost_VERSION in x.y.z format. </policy/CMP0093>
   CMP0092: MSVC warning flags are not in CMAKE_{C,CXX}_FLAGS by default. </policy/CMP0092>
   CMP0091: MSVC runtime library flags are selected by an abstraction. </policy/CMP0091>
   CMP0090: export(PACKAGE) does not populate package registry by default. </policy/CMP0090>
   CMP0089: Compiler id for IBM Clang-based XL compilers is now XLClang. </policy/CMP0089>

Policies Introduced by CMake 3.14
=================================

.. toctree::
   :maxdepth: 1

   CMP0088: FindBISON runs bison in CMAKE_CURRENT_BINARY_DIR when executing. </policy/CMP0088>
   CMP0087: install(SCRIPT | CODE) supports generator expressions. </policy/CMP0087>
   CMP0086: UseSWIG honors SWIG_MODULE_NAME via -module flag. </policy/CMP0086>
   CMP0085: IN_LIST generator expression handles empty list items. </policy/CMP0085>
   CMP0084: The FindQt module does not exist for find_package(). </policy/CMP0084>
   CMP0083: Add PIE options when linking executable. </policy/CMP0083>
   CMP0082: Install rules from add_subdirectory() are interleaved with those in caller. </policy/CMP0082>


Policies Introduced by CMake 3.13
=================================

.. toctree::
   :maxdepth: 1

   CMP0081: Relative paths not allowed in LINK_DIRECTORIES target property. </policy/CMP0081>
   CMP0080: BundleUtilities cannot be included at configure time. </policy/CMP0080>
   CMP0079: target_link_libraries allows use with targets in other directories. </policy/CMP0079>
   CMP0078: UseSWIG generates standard target names. </policy/CMP0078>
   CMP0077: option() honors normal variables. </policy/CMP0077>
   CMP0076: target_sources() command converts relative paths to absolute. </policy/CMP0076>

Policies Introduced by CMake 3.12
=================================

.. toctree::
   :maxdepth: 1

   CMP0075: Include file check macros honor CMAKE_REQUIRED_LIBRARIES. </policy/CMP0075>
   CMP0074: find_package uses PackageName_ROOT variables. </policy/CMP0074>
   CMP0073: Do not produce legacy _LIB_DEPENDS cache entries. </policy/CMP0073>

Policies Introduced by CMake 3.11
=================================

.. toctree::
   :maxdepth: 1

   CMP0072: FindOpenGL prefers GLVND by default when available. </policy/CMP0072>

Policies Introduced by CMake 3.10
=================================

.. toctree::
   :maxdepth: 1

   CMP0071: Let AUTOMOC and AUTOUIC process GENERATED files. </policy/CMP0071>
   CMP0070: Define file(GENERATE) behavior for relative paths. </policy/CMP0070>

Policies Introduced by CMake 3.9
================================

.. toctree::
   :maxdepth: 1

   CMP0069: INTERPROCEDURAL_OPTIMIZATION is enforced when enabled. </policy/CMP0069>
   CMP0068: RPATH settings on macOS do not affect install_name. </policy/CMP0068>

Policies Introduced by CMake 3.8
================================

.. toctree::
   :maxdepth: 1

   CMP0067: Honor language standard in try_compile() source-file signature. </policy/CMP0067>

Policies Introduced by CMake 3.7
================================

.. toctree::
   :maxdepth: 1

   CMP0066: Honor per-config flags in try_compile() source-file signature. </policy/CMP0066>

Policies Introduced by CMake 3.4
================================

.. toctree::
   :maxdepth: 1

   CMP0065: Do not add flags to export symbols from executables without the ENABLE_EXPORTS target property. </policy/CMP0065>
   CMP0064: Support new TEST if() operator. </policy/CMP0064>

Policies Introduced by CMake 3.3
================================

.. toctree::
   :maxdepth: 1

   CMP0063: Honor visibility properties for all target types. </policy/CMP0063>
   CMP0062: Disallow install() of export() result. </policy/CMP0062>
   CMP0061: CTest does not by default tell make to ignore errors (-i). </policy/CMP0061>
   CMP0060: Link libraries by full path even in implicit directories. </policy/CMP0060>
   CMP0059: Do not treat DEFINITIONS as a built-in directory property. </policy/CMP0059>
   CMP0058: Ninja requires custom command byproducts to be explicit. </policy/CMP0058>
   CMP0057: Support new IN_LIST if() operator. </policy/CMP0057>

Policies Introduced by CMake 3.2
================================

.. toctree::
   :maxdepth: 1

   CMP0056: Honor link flags in try_compile() source-file signature. </policy/CMP0056>
   CMP0055: Strict checking for break() command. </policy/CMP0055>

Policies Introduced by CMake 3.1
================================

.. toctree::
   :maxdepth: 1

   CMP0054: Only interpret if() arguments as variables or keywords when unquoted. </policy/CMP0054>
   CMP0053: Simplify variable reference and escape sequence evaluation. </policy/CMP0053>
   CMP0052: Reject source and build dirs in installed INTERFACE_INCLUDE_DIRECTORIES. </policy/CMP0052>
   CMP0051: List TARGET_OBJECTS in SOURCES target property. </policy/CMP0051>

Policies Introduced by CMake 3.0
================================

.. toctree::
   :maxdepth: 1

   CMP0050: Disallow add_custom_command SOURCE signatures. </policy/CMP0050>
   CMP0049: Do not expand variables in target source entries. </policy/CMP0049>
   CMP0048: project() command manages VERSION variables. </policy/CMP0048>
   CMP0047: Use QCC compiler id for the qcc drivers on QNX. </policy/CMP0047>
   CMP0046: Error on non-existent dependency in add_dependencies. </policy/CMP0046>
   CMP0045: Error on non-existent target in get_target_property. </policy/CMP0045>
   CMP0044: Case sensitive Lang_COMPILER_ID generator expressions. </policy/CMP0044>
   CMP0043: Ignore COMPILE_DEFINITIONS_Config properties. </policy/CMP0043>
   CMP0042: MACOSX_RPATH is enabled by default. </policy/CMP0042>
   CMP0041: Error on relative include with generator expression. </policy/CMP0041>
   CMP0040: The target in the TARGET signature of add_custom_command() must exist. </policy/CMP0040>
   CMP0039: Utility targets may not have link dependencies. </policy/CMP0039>
   CMP0038: Targets may not link directly to themselves. </policy/CMP0038>
   CMP0037: Target names should not be reserved and should match a validity pattern. </policy/CMP0037>
   CMP0036: The build_name command should not be called. </policy/CMP0036>
   CMP0035: The variable_requires command should not be called. </policy/CMP0035>
   CMP0034: The utility_source command should not be called. </policy/CMP0034>
   CMP0033: The export_library_dependencies command should not be called. </policy/CMP0033>
   CMP0032: The output_required_files command should not be called. </policy/CMP0032>
   CMP0031: The load_command command should not be called. </policy/CMP0031>
   CMP0030: The use_mangled_mesa command should not be called. </policy/CMP0030>
   CMP0029: The subdir_depends command should not be called. </policy/CMP0029>
   CMP0028: Double colon in target name means ALIAS or IMPORTED target. </policy/CMP0028>
   CMP0027: Conditionally linked imported targets with missing include directories. </policy/CMP0027>
   CMP0026: Disallow use of the LOCATION target property. </policy/CMP0026>
   CMP0025: Compiler id for Apple Clang is now AppleClang. </policy/CMP0025>
   CMP0024: Disallow include export result. </policy/CMP0024>

Policies Introduced by CMake 2.8
================================

.. toctree::
   :maxdepth: 1

   CMP0023: Plain and keyword target_link_libraries signatures cannot be mixed. </policy/CMP0023>
   CMP0022: INTERFACE_LINK_LIBRARIES defines the link interface. </policy/CMP0022>
   CMP0021: Fatal error on relative paths in INCLUDE_DIRECTORIES target property. </policy/CMP0021>
   CMP0020: Automatically link Qt executables to qtmain target on Windows. </policy/CMP0020>
   CMP0019: Do not re-expand variables in include and link information. </policy/CMP0019>
   CMP0018: Ignore CMAKE_SHARED_LIBRARY_Lang_FLAGS variable. </policy/CMP0018>
   CMP0017: Prefer files from the CMake module directory when including from there. </policy/CMP0017>
   CMP0016: target_link_libraries() reports error if its only argument is not a target. </policy/CMP0016>
   CMP0015: link_directories() treats paths relative to the source dir. </policy/CMP0015>
   CMP0014: Input directories must have CMakeLists.txt. </policy/CMP0014>
   CMP0013: Duplicate binary directories are not allowed. </policy/CMP0013>
   CMP0012: if() recognizes numbers and boolean constants. </policy/CMP0012>

Policies Introduced by CMake 2.6
================================

.. toctree::
   :maxdepth: 1

   CMP0011: Included scripts do automatic cmake_policy PUSH and POP. </policy/CMP0011>
   CMP0010: Bad variable reference syntax is an error. </policy/CMP0010>
   CMP0009: FILE GLOB_RECURSE calls should not follow symlinks by default. </policy/CMP0009>
   CMP0008: Libraries linked by full-path must have a valid library file name. </policy/CMP0008>
   CMP0007: list command no longer ignores empty elements. </policy/CMP0007>
   CMP0006: Installing MACOSX_BUNDLE targets requires a BUNDLE DESTINATION. </policy/CMP0006>
   CMP0005: Preprocessor definition values are now escaped automatically. </policy/CMP0005>
   CMP0004: Libraries linked may not have leading or trailing whitespace. </policy/CMP0004>
   CMP0003: Libraries linked via full path no longer produce linker search paths. </policy/CMP0003>
   CMP0002: Logical target names must be globally unique. </policy/CMP0002>
   CMP0001: CMAKE_BACKWARDS_COMPATIBILITY should no longer be used. </policy/CMP0001>
   CMP0000: A minimum required CMake version must be specified. </policy/CMP0000>
