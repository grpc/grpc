# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FetchContent
------------------

.. versionadded:: 3.11

.. only:: html

  .. contents::

Overview
^^^^^^^^

This module enables populating content at configure time via any method
supported by the :module:`ExternalProject` module.  Whereas
:command:`ExternalProject_Add` downloads at build time, the
``FetchContent`` module makes content available immediately, allowing the
configure step to use the content in commands like :command:`add_subdirectory`,
:command:`include` or :command:`file` operations.

Content population details would normally be defined separately from the
command that performs the actual population.  This separation ensures that
all of the dependency details are defined before anything may try to use those
details to populate content.  This is particularly important in more complex
project hierarchies where dependencies may be shared between multiple projects.

The following shows a typical example of declaring content details:

.. code-block:: cmake

  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        release-1.8.0
  )

For most typical cases, populating the content can then be done with a single
command like so:

.. code-block:: cmake

  FetchContent_MakeAvailable(googletest)

The above command not only populates the content, it also adds it to the main
build (if possible) so that the main build can use the populated project's
targets, etc.  In some cases, the main project may need to have more precise
control over the population or may be required to explicitly define the
population steps (e.g. if CMake versions earlier than 3.14 need to be
supported).  The typical pattern of such custom steps looks like this:

.. code-block:: cmake

  FetchContent_GetProperties(googletest)
  if(NOT googletest_POPULATED)
    FetchContent_Populate(googletest)
    add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})
  endif()

Regardless of which population method is used, when using the
declare-populate pattern with a hierarchical project arrangement, projects at
higher levels in the hierarchy are able to override the population details of
content specified anywhere lower in the project hierarchy.  The ability to
detect whether content has already been populated ensures that even if
multiple child projects want certain content to be available, the first one
to populate it wins.  The other child project can simply make use of the
already available content instead of repeating the population for itself.
See the :ref:`Examples <fetch-content-examples>` section which demonstrates
this scenario.

The ``FetchContent`` module also supports defining and populating
content in a single call, with no check for whether the content has been
populated elsewhere in the project already.  This is a more low level
operation and would not normally be the way the module is used, but it is
sometimes useful as part of implementing some higher level feature or to
populate some content in CMake's script mode.


Commands
^^^^^^^^

Declaring Content Details
"""""""""""""""""""""""""

.. command:: FetchContent_Declare

  .. code-block:: cmake

    FetchContent_Declare(<name> <contentOptions>...)

  The ``FetchContent_Declare()`` function records the options that describe
  how to populate the specified content, but if such details have already
  been recorded earlier in this project (regardless of where in the project
  hierarchy), this and all later calls for the same content ``<name>`` are
  ignored.  This "first to record, wins" approach is what allows hierarchical
  projects to have parent projects override content details of child projects.

  The content ``<name>`` can be any string without spaces, but good practice
  would be to use only letters, numbers and underscores.  The name will be
  treated case-insensitively and it should be obvious for the content it
  represents, often being the name of the child project or the value given
  to its top level :command:`project` command (if it is a CMake project).
  For well-known public projects, the name should generally be the official
  name of the project.  Choosing an unusual name makes it unlikely that other
  projects needing that same content will use the same name, leading to
  the content being populated multiple times.

  The ``<contentOptions>`` can be any of the download or update/patch options
  that the :command:`ExternalProject_Add` command understands.  The configure,
  build, install and test steps are explicitly disabled and therefore options
  related to them will be ignored.  The ``SOURCE_SUBDIR`` option is an
  exception, see :command:`FetchContent_MakeAvailable` for details on how that
  affects behavior.

  In most cases, ``<contentOptions>`` will just be a couple of options defining
  the download method and method-specific details like a commit tag or archive
  hash.  For example:

  .. code-block:: cmake

    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG        release-1.8.0
    )

    FetchContent_Declare(
      myCompanyIcons
      URL      https://intranet.mycompany.com/assets/iconset_1.12.tar.gz
      URL_HASH 5588a7b18261c20068beabfb4f530b87
    )

    FetchContent_Declare(
      myCompanyCertificates
      SVN_REPOSITORY svn+ssh://svn.mycompany.com/srv/svn/trunk/certs
      SVN_REVISION   -r12345
    )

Populating The Content
""""""""""""""""""""""

For most common scenarios, population means making content available to the
main build according to previously declared details for that dependency.
There are two main patterns for populating content, one based on calling
:command:`FetchContent_GetProperties` and
:command:`FetchContent_Populate` for more precise control and the other on
calling :command:`FetchContent_MakeAvailable` for a simpler, more automated
approach.  The former generally follows this canonical pattern:

.. _`fetch-content-canonical-pattern`:

.. code-block:: cmake

  # Check if population has already been performed
  FetchContent_GetProperties(<name>)
  string(TOLOWER "<name>" lcName)
  if(NOT ${lcName}_POPULATED)
    # Fetch the content using previously declared details
    FetchContent_Populate(<name>)

    # Set custom variables, policies, etc.
    # ...

    # Bring the populated content into the build
    add_subdirectory(${${lcName}_SOURCE_DIR} ${${lcName}_BINARY_DIR})
  endif()

The above is such a common pattern that, where no custom steps are needed
between the calls to :command:`FetchContent_Populate` and
:command:`add_subdirectory`, equivalent logic can be obtained by calling
:command:`FetchContent_MakeAvailable` instead.  Where it meets the needs of
the project, :command:`FetchContent_MakeAvailable` should be preferred, as it
is simpler and provides additional features over the pattern above.

.. command:: FetchContent_Populate

  .. code-block:: cmake

    FetchContent_Populate( <name> )

  In most cases, the only argument given to ``FetchContent_Populate()`` is the
  ``<name>``.  When used this way, the command assumes the content details have
  been recorded by an earlier call to :command:`FetchContent_Declare`.  The
  details are stored in a global property, so they are unaffected by things
  like variable or directory scope.  Therefore, it doesn't matter where in the
  project the details were previously declared, as long as they have been
  declared before the call to ``FetchContent_Populate()``.  Those saved details
  are then used to construct a call to :command:`ExternalProject_Add` in a
  private sub-build to perform the content population immediately.  The
  implementation of ``ExternalProject_Add()`` ensures that if the content has
  already been populated in a previous CMake run, that content will be reused
  rather than repopulating them again.  For the common case where population
  involves downloading content, the cost of the download is only paid once.

  An internal global property records when a particular content population
  request has been processed.  If ``FetchContent_Populate()`` is called more
  than once for the same content name within a configure run, the second call
  will halt with an error.  Projects can and should check whether content
  population has already been processed with the
  :command:`FetchContent_GetProperties` command before calling
  ``FetchContent_Populate()``.

  ``FetchContent_Populate()`` will set three variables in the scope of the
  caller; ``<lcName>_POPULATED``, ``<lcName>_SOURCE_DIR`` and
  ``<lcName>_BINARY_DIR``, where ``<lcName>`` is the lowercased ``<name>``.
  ``<lcName>_POPULATED`` will always be set to ``True`` by the call.
  ``<lcName>_SOURCE_DIR`` is the location where the
  content can be found upon return (it will have already been populated), while
  ``<lcName>_BINARY_DIR`` is a directory intended for use as a corresponding
  build directory.  The main use case for the two directory variables is to
  call :command:`add_subdirectory` immediately after population, i.e.:

  .. code-block:: cmake

    FetchContent_Populate(FooBar ...)
    add_subdirectory(${foobar_SOURCE_DIR} ${foobar_BINARY_DIR})

  The values of the three variables can also be retrieved from anywhere in the
  project hierarchy using the :command:`FetchContent_GetProperties` command.

  A number of cache variables influence the behavior of all content population
  performed using details saved from a :command:`FetchContent_Declare` call:

  ``FETCHCONTENT_BASE_DIR``
    In most cases, the saved details do not specify any options relating to the
    directories to use for the internal sub-build, final source and build areas.
    It is generally best to leave these decisions up to the ``FetchContent``
    module to handle on the project's behalf.  The ``FETCHCONTENT_BASE_DIR``
    cache variable controls the point under which all content population
    directories are collected, but in most cases developers would not need to
    change this.  The default location is ``${CMAKE_BINARY_DIR}/_deps``, but if
    developers change this value, they should aim to keep the path short and
    just below the top level of the build tree to avoid running into path
    length problems on Windows.

  ``FETCHCONTENT_QUIET``
    The logging output during population can be quite verbose, making the
    configure stage quite noisy.  This cache option (``ON`` by default) hides
    all population output unless an error is encountered.  If experiencing
    problems with hung downloads, temporarily switching this option off may
    help diagnose which content population is causing the issue.

  ``FETCHCONTENT_FULLY_DISCONNECTED``
    When this option is enabled, no attempt is made to download or update
    any content.  It is assumed that all content has already been populated in
    a previous run or the source directories have been pointed at existing
    contents the developer has provided manually (using options described
    further below).  When the developer knows that no changes have been made to
    any content details, turning this option ``ON`` can significantly speed up
    the configure stage.  It is ``OFF`` by default.

  ``FETCHCONTENT_UPDATES_DISCONNECTED``
    This is a less severe download/update control compared to
    ``FETCHCONTENT_FULLY_DISCONNECTED``.  Instead of bypassing all download and
    update logic, the ``FETCHCONTENT_UPDATES_DISCONNECTED`` only disables the
    update stage.  Therefore, if content has not been downloaded previously,
    it will still be downloaded when this option is enabled.  This can speed up
    the configure stage, but not as much as
    ``FETCHCONTENT_FULLY_DISCONNECTED``.  It is ``OFF`` by default.

  In addition to the above cache variables, the following cache variables are
  also defined for each content name (``<ucName>`` is the uppercased value of
  ``<name>``):

  ``FETCHCONTENT_SOURCE_DIR_<ucName>``
    If this is set, no download or update steps are performed for the specified
    content and the ``<lcName>_SOURCE_DIR`` variable returned to the caller is
    pointed at this location.  This gives developers a way to have a separate
    checkout of the content that they can modify freely without interference
    from the build.  The build simply uses that existing source, but it still
    defines ``<lcName>_BINARY_DIR`` to point inside its own build area.
    Developers are strongly encouraged to use this mechanism rather than
    editing the sources populated in the default location, as changes to
    sources in the default location can be lost when content population details
    are changed by the project.

  ``FETCHCONTENT_UPDATES_DISCONNECTED_<ucName>``
    This is the per-content equivalent of
    ``FETCHCONTENT_UPDATES_DISCONNECTED``. If the global option or this option
    is ``ON``, then updates will be disabled for the named content.
    Disabling updates for individual content can be useful for content whose
    details rarely change, while still leaving other frequently changing
    content with updates enabled.


  The ``FetchContent_Populate()`` command also supports a syntax allowing the
  content details to be specified directly rather than using any saved
  details.  This is more low-level and use of this form is generally to be
  avoided in favour of using saved content details as outlined above.
  Nevertheless, in certain situations it can be useful to invoke the content
  population as an isolated operation (typically as part of implementing some
  other higher level feature or when using CMake in script mode):

  .. code-block:: cmake

    FetchContent_Populate( <name>
      [QUIET]
      [SUBBUILD_DIR <subBuildDir>]
      [SOURCE_DIR <srcDir>]
      [BINARY_DIR <binDir>]
      ...
    )

  This form has a number of key differences to that where only ``<name>`` is
  provided:

  - All required population details are assumed to have been provided directly
    in the call to ``FetchContent_Populate()``. Any saved details for
    ``<name>`` are ignored.
  - No check is made for whether content for ``<name>`` has already been
    populated.
  - No global property is set to record that the population has occurred.
  - No global properties record the source or binary directories used for the
    populated content.
  - The ``FETCHCONTENT_FULLY_DISCONNECTED`` and
    ``FETCHCONTENT_UPDATES_DISCONNECTED`` cache variables are ignored.

  The ``<lcName>_SOURCE_DIR`` and ``<lcName>_BINARY_DIR`` variables are still
  returned to the caller, but since these locations are not stored as global
  properties when this form is used, they are only available to the calling
  scope and below rather than the entire project hierarchy.  No
  ``<lcName>_POPULATED`` variable is set in the caller's scope with this form.

  The supported options for ``FetchContent_Populate()`` are the same as those
  for :command:`FetchContent_Declare()`.  Those few options shown just
  above are either specific to ``FetchContent_Populate()`` or their behavior is
  slightly modified from how :command:`ExternalProject_Add` treats them.

  ``QUIET``
    The ``QUIET`` option can be given to hide the output associated with
    populating the specified content.  If the population fails, the output will
    be shown regardless of whether this option was given or not so that the
    cause of the failure can be diagnosed.  The global ``FETCHCONTENT_QUIET``
    cache variable has no effect on ``FetchContent_Populate()`` calls where the
    content details are provided directly.

  ``SUBBUILD_DIR``
    The ``SUBBUILD_DIR`` argument can be provided to change the location of the
    sub-build created to perform the population.  The default value is
    ``${CMAKE_CURRENT_BINARY_DIR}/<lcName>-subbuild`` and it would be unusual
    to need to override this default.  If a relative path is specified, it will
    be interpreted as relative to :variable:`CMAKE_CURRENT_BINARY_DIR`.
    This option should not be confused with the ``SOURCE_SUBDIR`` option which
    only affects the :command:`FetchContent_MakeAvailable` command.

  ``SOURCE_DIR``, ``BINARY_DIR``
    The ``SOURCE_DIR`` and ``BINARY_DIR`` arguments are supported by
    :command:`ExternalProject_Add`, but different default values are used by
    ``FetchContent_Populate()``.  ``SOURCE_DIR`` defaults to
    ``${CMAKE_CURRENT_BINARY_DIR}/<lcName>-src`` and ``BINARY_DIR`` defaults to
    ``${CMAKE_CURRENT_BINARY_DIR}/<lcName>-build``.  If a relative path is
    specified, it will be interpreted as relative to
    :variable:`CMAKE_CURRENT_BINARY_DIR`.

  In addition to the above explicit options, any other unrecognized options are
  passed through unmodified to :command:`ExternalProject_Add` to perform the
  download, patch and update steps.  The following options are explicitly
  prohibited (they are disabled by the ``FetchContent_Populate()`` command):

  - ``CONFIGURE_COMMAND``
  - ``BUILD_COMMAND``
  - ``INSTALL_COMMAND``
  - ``TEST_COMMAND``

  If using ``FetchContent_Populate()`` within CMake's script mode, be aware
  that the implementation sets up a sub-build which therefore requires a CMake
  generator and build tool to be available. If these cannot be found by
  default, then the :variable:`CMAKE_GENERATOR` and/or
  :variable:`CMAKE_MAKE_PROGRAM` variables will need to be set appropriately
  on the command line invoking the script.


.. command:: FetchContent_GetProperties

  When using saved content details, a call to :command:`FetchContent_Populate`
  records information in global properties which can be queried at any time.
  This information includes the source and binary directories associated with
  the content and also whether or not the content population has been processed
  during the current configure run.

  .. code-block:: cmake

    FetchContent_GetProperties( <name>
      [SOURCE_DIR <srcDirVar>]
      [BINARY_DIR <binDirVar>]
      [POPULATED <doneVar>]
    )

  The ``SOURCE_DIR``, ``BINARY_DIR`` and ``POPULATED`` options can be used to
  specify which properties should be retrieved.  Each option accepts a value
  which is the name of the variable in which to store that property.  Most of
  the time though, only ``<name>`` is given, in which case the call will then
  set the same variables as a call to
  :command:`FetchContent_Populate(name) <FetchContent_Populate>`.  This allows
  the following canonical pattern to be used, which ensures that the relevant
  variables will always be defined regardless of whether or not the population
  has been performed elsewhere in the project already:

  .. code-block:: cmake

    FetchContent_GetProperties(foobar)
    if(NOT foobar_POPULATED)
      FetchContent_Populate(foobar)
      ...
    endif()

  The above pattern allows other parts of the overall project hierarchy to
  re-use the same content and ensure that it is only populated once.


.. command:: FetchContent_MakeAvailable

  .. code-block:: cmake

    FetchContent_MakeAvailable( <name1> [<name2>...] )

  This command implements the common pattern typically needed for most
  dependencies.  It iterates over each of the named dependencies in turn
  and for each one it loosely follows the
  :ref:`canonical pattern <fetch-content-canonical-pattern>` as
  presented at the beginning of this section.  An important difference is
  that :command:`add_subdirectory` will only be called on the
  populated content if there is a ``CMakeLists.txt`` file in its top level
  source directory.  This allows the command to be used for dependencies
  that make downloaded content available at a known location but which do
  not need or support being added directly to the build.

  The ``SOURCE_SUBDIR`` option can be given in the declared details to
  instruct ``FetchContent_MakeAvailable()`` to look for a ``CMakeLists.txt``
  file in a subdirectory below the top level (i.e. the same way that
  ``SOURCE_SUBDIR`` is used by the :command:`ExternalProject_Add` command).
  ``SOURCE_SUBDIR`` must always be a relative path.  See the next section
  for an example of this option.


.. _`fetch-content-examples`:

Examples
^^^^^^^^

This first fairly straightforward example ensures that some popular testing
frameworks are available to the main build:

.. code-block:: cmake

  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        release-1.8.0
  )
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v2.5.0
  )

  # After the following call, the CMake targets defined by googletest and
  # Catch2 will be defined and available to the rest of the build
  FetchContent_MakeAvailable(googletest Catch2)

If the sub-project's ``CMakeLists.txt`` file is not at the top level of its
source tree, the ``SOURCE_SUBDIR`` option can be used to tell ``FetchContent``
where to find it.  The following example shows how to use that option and
it also sets a variable which is meaningful to the subproject before pulling
it into the main build:

.. code-block:: cmake

  include(FetchContent)
  FetchContent_Declare(
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG        v3.12.0
    SOURCE_SUBDIR  cmake
  )
  set(protobuf_BUILD_TESTS OFF)
  FetchContent_MakeAvailable(protobuf)

In more complex project hierarchies, the dependency relationships can be more
complicated.  Consider a hierarchy where ``projA`` is the top level project and
it depends directly on projects ``projB`` and ``projC``.  Both ``projB`` and
``projC`` can be built standalone and they also both depend on another project
``projD``.  ``projB`` additionally depends on ``projE``.  This example assumes
that all five projects are available on a company git server.  The
``CMakeLists.txt`` of each project might have sections like the following:

*projA*:

.. code-block:: cmake

  include(FetchContent)
  FetchContent_Declare(
    projB
    GIT_REPOSITORY git@mycompany.com:git/projB.git
    GIT_TAG        4a89dc7e24ff212a7b5167bef7ab079d
  )
  FetchContent_Declare(
    projC
    GIT_REPOSITORY git@mycompany.com:git/projC.git
    GIT_TAG        4ad4016bd1d8d5412d135cf8ceea1bb9
  )
  FetchContent_Declare(
    projD
    GIT_REPOSITORY git@mycompany.com:git/projD.git
    GIT_TAG        origin/integrationBranch
  )
  FetchContent_Declare(
    projE
    GIT_REPOSITORY git@mycompany.com:git/projE.git
    GIT_TAG        origin/release/2.3-rc1
  )

  # Order is important, see notes in the discussion further below
  FetchContent_MakeAvailable(projD projB projC)

*projB*:

.. code-block:: cmake

  include(FetchContent)
  FetchContent_Declare(
    projD
    GIT_REPOSITORY git@mycompany.com:git/projD.git
    GIT_TAG        20b415f9034bbd2a2e8216e9a5c9e632
  )
  FetchContent_Declare(
    projE
    GIT_REPOSITORY git@mycompany.com:git/projE.git
    GIT_TAG        68e20f674a48be38d60e129f600faf7d
  )

  FetchContent_MakeAvailable(projD projE)

*projC*:

.. code-block:: cmake

  include(FetchContent)
  FetchContent_Declare(
    projD
    GIT_REPOSITORY git@mycompany.com:git/projD.git
    GIT_TAG        7d9a17ad2c962aa13e2fbb8043fb6b8a
  )

  # This particular version of projD requires workarounds
  FetchContent_GetProperties(projD)
  if(NOT projd_POPULATED)
    FetchContent_Populate(projD)

    # Copy an additional/replacement file into the populated source
    file(COPY someFile.c DESTINATION ${projd_SOURCE_DIR}/src)

    add_subdirectory(${projd_SOURCE_DIR} ${projd_BINARY_DIR})
  endif()

A few key points should be noted in the above:

- ``projB`` and ``projC`` define different content details for ``projD``,
  but ``projA`` also defines a set of content details for ``projD``.
  Because ``projA`` will define them first, the details from ``projB`` and
  ``projC`` will not be used.  The override details defined by ``projA``
  are not required to match either of those from ``projB`` or ``projC``, but
  it is up to the higher level project to ensure that the details it does
  define still make sense for the child projects.
- In the ``projA`` call to :command:`FetchContent_MakeAvailable`, ``projD``
  is listed ahead of ``projB`` and ``projC`` to ensure that ``projA`` is in
  control of how ``projD`` is populated.
- While ``projA`` defines content details for ``projE``, it does not need
  to explicitly call ``FetchContent_MakeAvailable(projE)`` or
  ``FetchContent_Populate(projD)`` itself.  Instead, it leaves that to the
  child ``projB``.  For higher level projects, it is often enough to just
  define the override content details and leave the actual population to the
  child projects.  This saves repeating the same thing at each level of the
  project hierarchy unnecessarily.


Projects don't always need to add the populated content to the build.
Sometimes the project just wants to make the downloaded content available at
a predictable location.  The next example ensures that a set of standard
company toolchain files (and potentially even the toolchain binaries
themselves) is available early enough to be used for that same build.

.. code-block:: cmake

  cmake_minimum_required(VERSION 3.14)

  include(FetchContent)
  FetchContent_Declare(
    mycom_toolchains
    URL  https://intranet.mycompany.com//toolchains_1.3.2.tar.gz
  )
  FetchContent_MakeAvailable(mycom_toolchains)

  project(CrossCompileExample)

The project could be configured to use one of the downloaded toolchains like
so:

.. code-block:: shell

  cmake -DCMAKE_TOOLCHAIN_FILE=_deps/mycom_toolchains-src/toolchain_arm.cmake /path/to/src

When CMake processes the ``CMakeLists.txt`` file, it will download and unpack
the tarball into ``_deps/mycompany_toolchains-src`` relative to the build
directory.  The :variable:`CMAKE_TOOLCHAIN_FILE` variable is not used until
the :command:`project` command is reached, at which point CMake looks for the
named toolchain file relative to the build directory.  Because the tarball has
already been downloaded and unpacked by then, the toolchain file will be in
place, even the very first time that ``cmake`` is run in the build directory.

Lastly, the following example demonstrates how one might download and unpack a
firmware tarball using CMake's :manual:`script mode <cmake(1)>`.  The call to
:command:`FetchContent_Populate` specifies all the content details and the
unpacked firmware will be placed in a ``firmware`` directory below the
current working directory.

*getFirmware.cmake*:

.. code-block:: cmake

  # NOTE: Intended to be run in script mode with cmake -P
  include(FetchContent)
  FetchContent_Populate(
    firmware
    URL        https://mycompany.com/assets/firmware-1.23-arm.tar.gz
    URL_HASH   MD5=68247684da89b608d466253762b0ff11
    SOURCE_DIR firmware
  )

#]=======================================================================]

#=======================================================================
# Recording and retrieving content details for later population
#=======================================================================

# Internal use, projects must not call this directly. It is
# intended for use by FetchContent_Declare() only.
#
# Sets a content-specific global property (not meant for use
# outside of functions defined here in this file) which can later
# be retrieved using __FetchContent_getSavedDetails() with just the
# same content name. If there is already a value stored in the
# property, it is left unchanged and this call has no effect.
# This allows parent projects to define the content details,
# overriding anything a child project may try to set (properties
# are not cached between runs, so the first thing to set it in a
# build will be in control).
function(__FetchContent_declareDetails contentName)

  string(TOLOWER ${contentName} contentNameLower)
  set(propertyName "_FetchContent_${contentNameLower}_savedDetails")
  get_property(alreadyDefined GLOBAL PROPERTY ${propertyName} DEFINED)
  if(NOT alreadyDefined)
    define_property(GLOBAL PROPERTY ${propertyName}
      BRIEF_DOCS "Internal implementation detail of FetchContent_Populate()"
      FULL_DOCS  "Details used by FetchContent_Populate() for ${contentName}"
    )
    set(__cmdArgs)
    foreach(__item IN LISTS ARGN)
      string(APPEND __cmdArgs " [==[${__item}]==]")
    endforeach()
    cmake_language(EVAL CODE
      "set_property(GLOBAL PROPERTY ${propertyName} ${__cmdArgs})")
  endif()

endfunction()


# Internal use, projects must not call this directly. It is
# intended for use by the FetchContent_Declare() function.
#
# Retrieves details saved for the specified content in an
# earlier call to __FetchContent_declareDetails().
function(__FetchContent_getSavedDetails contentName outVar)

  string(TOLOWER ${contentName} contentNameLower)
  set(propertyName "_FetchContent_${contentNameLower}_savedDetails")
  get_property(alreadyDefined GLOBAL PROPERTY ${propertyName} DEFINED)
  if(NOT alreadyDefined)
    message(FATAL_ERROR "No content details recorded for ${contentName}")
  endif()
  get_property(propertyValue GLOBAL PROPERTY ${propertyName})
  set(${outVar} "${propertyValue}" PARENT_SCOPE)

endfunction()


# Saves population details of the content, sets defaults for the
# SOURCE_DIR and BUILD_DIR.
function(FetchContent_Declare contentName)

  set(options "")
  set(oneValueArgs SVN_REPOSITORY)
  set(multiValueArgs "")

  cmake_parse_arguments(PARSE_ARGV 1 ARG
    "${options}" "${oneValueArgs}" "${multiValueArgs}")

  unset(srcDirSuffix)
  unset(svnRepoArgs)
  if(ARG_SVN_REPOSITORY)
    # Add a hash of the svn repository URL to the source dir. This works
    # around the problem where if the URL changes, the download would
    # fail because it tries to checkout/update rather than switch the
    # old URL to the new one. We limit the hash to the first 7 characters
    # so that the source path doesn't get overly long (which can be a
    # problem on windows due to path length limits).
    string(SHA1 urlSHA ${ARG_SVN_REPOSITORY})
    string(SUBSTRING ${urlSHA} 0 7 urlSHA)
    set(srcDirSuffix "-${urlSHA}")
    set(svnRepoArgs  SVN_REPOSITORY ${ARG_SVN_REPOSITORY})
  endif()

  string(TOLOWER ${contentName} contentNameLower)

  set(__argsQuoted)
  foreach(__item IN LISTS ARG_UNPARSED_ARGUMENTS)
    string(APPEND __argsQuoted " [==[${__item}]==]")
  endforeach()
  cmake_language(EVAL CODE "
    __FetchContent_declareDetails(
      ${contentNameLower}
      SOURCE_DIR \"${FETCHCONTENT_BASE_DIR}/${contentNameLower}-src${srcDirSuffix}\"
      BINARY_DIR \"${FETCHCONTENT_BASE_DIR}/${contentNameLower}-build\"
      \${svnRepoArgs}
      # List these last so they can override things we set above
      ${__argsQuoted}
    )"
  )

endfunction()


#=======================================================================
# Set/get whether the specified content has been populated yet.
# The setter also records the source and binary dirs used.
#=======================================================================

# Internal use, projects must not call this directly. It is
# intended for use by the FetchContent_Populate() function to
# record when FetchContent_Populate() is called for a particular
# content name.
function(__FetchContent_setPopulated contentName sourceDir binaryDir)

  string(TOLOWER ${contentName} contentNameLower)
  set(prefix "_FetchContent_${contentNameLower}")

  set(propertyName "${prefix}_sourceDir")
  define_property(GLOBAL PROPERTY ${propertyName}
    BRIEF_DOCS "Internal implementation detail of FetchContent_Populate()"
    FULL_DOCS  "Details used by FetchContent_Populate() for ${contentName}"
  )
  set_property(GLOBAL PROPERTY ${propertyName} ${sourceDir})

  set(propertyName "${prefix}_binaryDir")
  define_property(GLOBAL PROPERTY ${propertyName}
    BRIEF_DOCS "Internal implementation detail of FetchContent_Populate()"
    FULL_DOCS  "Details used by FetchContent_Populate() for ${contentName}"
  )
  set_property(GLOBAL PROPERTY ${propertyName} ${binaryDir})

  set(propertyName "${prefix}_populated")
  define_property(GLOBAL PROPERTY ${propertyName}
    BRIEF_DOCS "Internal implementation detail of FetchContent_Populate()"
    FULL_DOCS  "Details used by FetchContent_Populate() for ${contentName}"
  )
  set_property(GLOBAL PROPERTY ${propertyName} True)

endfunction()


# Set variables in the calling scope for any of the retrievable
# properties. If no specific properties are requested, variables
# will be set for all retrievable properties.
#
# This function is intended to also be used by projects as the canonical
# way to detect whether they should call FetchContent_Populate()
# and pull the populated source into the build with add_subdirectory(),
# if they are using the populated content in that way.
function(FetchContent_GetProperties contentName)

  string(TOLOWER ${contentName} contentNameLower)

  set(options "")
  set(oneValueArgs SOURCE_DIR BINARY_DIR POPULATED)
  set(multiValueArgs "")

  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT ARG_SOURCE_DIR AND
     NOT ARG_BINARY_DIR AND
     NOT ARG_POPULATED)
    # No specific properties requested, provide them all
    set(ARG_SOURCE_DIR ${contentNameLower}_SOURCE_DIR)
    set(ARG_BINARY_DIR ${contentNameLower}_BINARY_DIR)
    set(ARG_POPULATED  ${contentNameLower}_POPULATED)
  endif()

  set(prefix "_FetchContent_${contentNameLower}")

  if(ARG_SOURCE_DIR)
    set(propertyName "${prefix}_sourceDir")
    get_property(value GLOBAL PROPERTY ${propertyName})
    if(value)
      set(${ARG_SOURCE_DIR} ${value} PARENT_SCOPE)
    endif()
  endif()

  if(ARG_BINARY_DIR)
    set(propertyName "${prefix}_binaryDir")
    get_property(value GLOBAL PROPERTY ${propertyName})
    if(value)
      set(${ARG_BINARY_DIR} ${value} PARENT_SCOPE)
    endif()
  endif()

  if(ARG_POPULATED)
    set(propertyName "${prefix}_populated")
    get_property(value GLOBAL PROPERTY ${propertyName} DEFINED)
    set(${ARG_POPULATED} ${value} PARENT_SCOPE)
  endif()

endfunction()


#=======================================================================
# Performing the population
#=======================================================================

# The value of contentName will always have been lowercased by the caller.
# All other arguments are assumed to be options that are understood by
# ExternalProject_Add(), except for QUIET and SUBBUILD_DIR.
function(__FetchContent_directPopulate contentName)

  set(options
      QUIET
  )
  set(oneValueArgs
      SUBBUILD_DIR
      SOURCE_DIR
      BINARY_DIR
      # We need special processing if DOWNLOAD_NO_EXTRACT is true
      DOWNLOAD_NO_EXTRACT
      # Prevent the following from being passed through
      CONFIGURE_COMMAND
      BUILD_COMMAND
      INSTALL_COMMAND
      TEST_COMMAND
      # We force both of these to be ON since we are always executing serially
      # and we want all steps to have access to the terminal in case they
      # need input from the command line (e.g. ask for a private key password)
      # or they want to provide timely progress. We silently absorb and
      # discard these if they are set by the caller.
      USES_TERMINAL_DOWNLOAD
      USES_TERMINAL_UPDATE
  )
  set(multiValueArgs "")

  cmake_parse_arguments(PARSE_ARGV 1 ARG
    "${options}" "${oneValueArgs}" "${multiValueArgs}")

  if(NOT ARG_SUBBUILD_DIR)
    message(FATAL_ERROR "Internal error: SUBBUILD_DIR not set")
  elseif(NOT IS_ABSOLUTE "${ARG_SUBBUILD_DIR}")
    set(ARG_SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/${ARG_SUBBUILD_DIR}")
  endif()

  if(NOT ARG_SOURCE_DIR)
    message(FATAL_ERROR "Internal error: SOURCE_DIR not set")
  elseif(NOT IS_ABSOLUTE "${ARG_SOURCE_DIR}")
    set(ARG_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/${ARG_SOURCE_DIR}")
  endif()

  if(NOT ARG_BINARY_DIR)
    message(FATAL_ERROR "Internal error: BINARY_DIR not set")
  elseif(NOT IS_ABSOLUTE "${ARG_BINARY_DIR}")
    set(ARG_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/${ARG_BINARY_DIR}")
  endif()

  # Ensure the caller can know where to find the source and build directories
  # with some convenient variables. Doing this here ensures the caller sees
  # the correct result in the case where the default values are overridden by
  # the content details set by the project.
  set(${contentName}_SOURCE_DIR "${ARG_SOURCE_DIR}" PARENT_SCOPE)
  set(${contentName}_BINARY_DIR "${ARG_BINARY_DIR}" PARENT_SCOPE)

  # The unparsed arguments may contain spaces, so build up ARG_EXTRA
  # in such a way that it correctly substitutes into the generated
  # CMakeLists.txt file with each argument quoted.
  unset(ARG_EXTRA)
  foreach(arg IN LISTS ARG_UNPARSED_ARGUMENTS)
    set(ARG_EXTRA "${ARG_EXTRA} \"${arg}\"")
  endforeach()

  if(ARG_DOWNLOAD_NO_EXTRACT)
    set(ARG_EXTRA "${ARG_EXTRA} DOWNLOAD_NO_EXTRACT YES")
    set(__FETCHCONTENT_COPY_FILE
"
ExternalProject_Get_Property(${contentName}-populate DOWNLOADED_FILE)
get_filename_component(dlFileName \"\${DOWNLOADED_FILE}\" NAME)

ExternalProject_Add_Step(${contentName}-populate copyfile
  COMMAND    \"${CMAKE_COMMAND}\" -E copy_if_different
             \"<DOWNLOADED_FILE>\" \"${ARG_SOURCE_DIR}\"
  DEPENDEES  patch
  DEPENDERS  configure
  BYPRODUCTS \"${ARG_SOURCE_DIR}/\${dlFileName}\"
  COMMENT    \"Copying file to SOURCE_DIR\"
)
")
  else()
    unset(__FETCHCONTENT_COPY_FILE)
  endif()

  # Hide output if requested, but save it to a variable in case there's an
  # error so we can show the output upon failure. When not quiet, don't
  # capture the output to a variable because the user may want to see the
  # output as it happens (e.g. progress during long downloads). Combine both
  # stdout and stderr in the one capture variable so the output stays in order.
  if (ARG_QUIET)
    set(outputOptions
        OUTPUT_VARIABLE capturedOutput
        ERROR_VARIABLE  capturedOutput
    )
  else()
    set(capturedOutput)
    set(outputOptions)
    message(STATUS "Populating ${contentName}")
  endif()

  if(CMAKE_GENERATOR)
    set(subCMakeOpts "-G${CMAKE_GENERATOR}")
    if(CMAKE_GENERATOR_PLATFORM)
      list(APPEND subCMakeOpts "-A${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
      list(APPEND subCMakeOpts "-T${CMAKE_GENERATOR_TOOLSET}")
    endif()

    if(CMAKE_MAKE_PROGRAM)
      list(APPEND subCMakeOpts "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}")
    endif()

  else()
    # Likely we've been invoked via CMake's script mode where no
    # generator is set (and hence CMAKE_MAKE_PROGRAM could not be
    # trusted even if provided). We will have to rely on being
    # able to find the default generator and build tool.
    unset(subCMakeOpts)
  endif()

  if(DEFINED CMAKE_EP_GIT_REMOTE_UPDATE_STRATEGY)
    list(APPEND subCMakeOpts
      "-DCMAKE_EP_GIT_REMOTE_UPDATE_STRATEGY=${CMAKE_EP_GIT_REMOTE_UPDATE_STRATEGY}")
  endif()

  # Create and build a separate CMake project to carry out the population.
  # If we've already previously done these steps, they will not cause
  # anything to be updated, so extra rebuilds of the project won't occur.
  # Make sure to pass through CMAKE_MAKE_PROGRAM in case the main project
  # has this set to something not findable on the PATH.
  configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/FetchContent/CMakeLists.cmake.in"
                 "${ARG_SUBBUILD_DIR}/CMakeLists.txt")
  execute_process(
    COMMAND ${CMAKE_COMMAND} ${subCMakeOpts} .
    RESULT_VARIABLE result
    ${outputOptions}
    WORKING_DIRECTORY "${ARG_SUBBUILD_DIR}"
  )
  if(result)
    if(capturedOutput)
      message("${capturedOutput}")
    endif()
    message(FATAL_ERROR "CMake step for ${contentName} failed: ${result}")
  endif()
  execute_process(
    COMMAND ${CMAKE_COMMAND} --build .
    RESULT_VARIABLE result
    ${outputOptions}
    WORKING_DIRECTORY "${ARG_SUBBUILD_DIR}"
  )
  if(result)
    if(capturedOutput)
      message("${capturedOutput}")
    endif()
    message(FATAL_ERROR "Build step for ${contentName} failed: ${result}")
  endif()

endfunction()


option(FETCHCONTENT_FULLY_DISCONNECTED   "Disables all attempts to download or update content and assumes source dirs already exist")
option(FETCHCONTENT_UPDATES_DISCONNECTED "Enables UPDATE_DISCONNECTED behavior for all content population")
option(FETCHCONTENT_QUIET                "Enables QUIET option for all content population" ON)
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps" CACHE PATH "Directory under which to collect all populated content")

# Populate the specified content using details stored from
# an earlier call to FetchContent_Declare().
function(FetchContent_Populate contentName)

  if(NOT contentName)
    message(FATAL_ERROR "Empty contentName not allowed for FetchContent_Populate()")
  endif()

  string(TOLOWER ${contentName} contentNameLower)

  if(ARGN)
    # This is the direct population form with details fully specified
    # as part of the call, so we already have everything we need
    __FetchContent_directPopulate(
      ${contentNameLower}
      SUBBUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/${contentNameLower}-subbuild"
      SOURCE_DIR   "${CMAKE_CURRENT_BINARY_DIR}/${contentNameLower}-src"
      BINARY_DIR   "${CMAKE_CURRENT_BINARY_DIR}/${contentNameLower}-build"
      ${ARGN}  # Could override any of the above ..._DIR variables
    )

    # Pass source and binary dir variables back to the caller
    set(${contentNameLower}_SOURCE_DIR "${${contentNameLower}_SOURCE_DIR}" PARENT_SCOPE)
    set(${contentNameLower}_BINARY_DIR "${${contentNameLower}_BINARY_DIR}" PARENT_SCOPE)

    # Don't set global properties, or record that we did this population, since
    # this was a direct call outside of the normal declared details form.
    # We only want to save values in the global properties for content that
    # honours the hierarchical details mechanism so that projects are not
    # robbed of the ability to override details set in nested projects.
    return()
  endif()

  # No details provided, so assume they were saved from an earlier call
  # to FetchContent_Declare(). Do a check that we haven't already
  # populated this content before in case the caller forgot to check.
  FetchContent_GetProperties(${contentName})
  if(${contentNameLower}_POPULATED)
    message(FATAL_ERROR "Content ${contentName} already populated in ${${contentNameLower}_SOURCE_DIR}")
  endif()

  __FetchContent_getSavedDetails(${contentName} contentDetails)
  if("${contentDetails}" STREQUAL "")
    message(FATAL_ERROR "No details have been set for content: ${contentName}")
  endif()

  string(TOUPPER ${contentName} contentNameUpper)
  set(FETCHCONTENT_SOURCE_DIR_${contentNameUpper}
      "${FETCHCONTENT_SOURCE_DIR_${contentNameUpper}}"
      CACHE PATH "When not empty, overrides where to find pre-populated content for ${contentName}")

  if(FETCHCONTENT_SOURCE_DIR_${contentNameUpper})
    # The source directory has been explicitly provided in the cache,
    # so no population is required. The build directory may still be specified
    # by the declared details though.

    if(NOT IS_ABSOLUTE "${FETCHCONTENT_SOURCE_DIR_${contentNameUpper}}")
      # Don't check this directory because we don't know what location it is
      # expected to be relative to. We can't make this a hard error for backward
      # compatibility reasons.
      message(WARNING "Relative source directory specified. This is not safe, "
        "as it depends on the calling directory scope.\n"
        "  FETCHCONTENT_SOURCE_DIR_${contentNameUpper} --> ${FETCHCONTENT_SOURCE_DIR_${contentNameUpper}}")
    elseif(NOT EXISTS "${FETCHCONTENT_SOURCE_DIR_${contentNameUpper}}")
      message(FATAL_ERROR "Manually specified source directory is missing:\n"
        "  FETCHCONTENT_SOURCE_DIR_${contentNameUpper} --> ${FETCHCONTENT_SOURCE_DIR_${contentNameUpper}}")
    endif()

    set(${contentNameLower}_SOURCE_DIR "${FETCHCONTENT_SOURCE_DIR_${contentNameUpper}}")

    cmake_parse_arguments(savedDetails "" "BINARY_DIR" "" ${contentDetails})

    if(savedDetails_BINARY_DIR)
      set(${contentNameLower}_BINARY_DIR ${savedDetails_BINARY_DIR})
    else()
      set(${contentNameLower}_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/${contentNameLower}-build")
    endif()

  elseif(FETCHCONTENT_FULLY_DISCONNECTED)
    # Bypass population and assume source is already there from a previous run.
    # Declared details may override the default source or build directories.

    cmake_parse_arguments(savedDetails "" "SOURCE_DIR;BINARY_DIR" "" ${contentDetails})

    if(savedDetails_SOURCE_DIR)
      set(${contentNameLower}_SOURCE_DIR ${savedDetails_SOURCE_DIR})
    else()
      set(${contentNameLower}_SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/${contentNameLower}-src")
    endif()

    if(savedDetails_BINARY_DIR)
      set(${contentNameLower}_BINARY_DIR ${savedDetails_BINARY_DIR})
    else()
      set(${contentNameLower}_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/${contentNameLower}-build")
    endif()

  else()
    # Support both a global "disconnect all updates" and a per-content
    # update test (either one being set disables updates for this content).
    option(FETCHCONTENT_UPDATES_DISCONNECTED_${contentNameUpper}
           "Enables UPDATE_DISCONNECTED behavior just for population of ${contentName}")
    if(FETCHCONTENT_UPDATES_DISCONNECTED OR
       FETCHCONTENT_UPDATES_DISCONNECTED_${contentNameUpper})
      set(disconnectUpdates True)
    else()
      set(disconnectUpdates False)
    endif()

    if(FETCHCONTENT_QUIET)
      set(quietFlag QUIET)
    else()
      unset(quietFlag)
    endif()

    set(__detailsQuoted)
    foreach(__item IN LISTS contentDetails)
      string(APPEND __detailsQuoted " [==[${__item}]==]")
    endforeach()
    cmake_language(EVAL CODE "
      __FetchContent_directPopulate(
        ${contentNameLower}
        ${quietFlag}
        UPDATE_DISCONNECTED ${disconnectUpdates}
        SUBBUILD_DIR \"${FETCHCONTENT_BASE_DIR}/${contentNameLower}-subbuild\"
        SOURCE_DIR   \"${FETCHCONTENT_BASE_DIR}/${contentNameLower}-src\"
        BINARY_DIR   \"${FETCHCONTENT_BASE_DIR}/${contentNameLower}-build\"
        # Put the saved details last so they can override any of the
        # the options we set above (this can include SOURCE_DIR or
        # BUILD_DIR)
        ${__detailsQuoted}
      )"
    )
  endif()

  __FetchContent_setPopulated(
    ${contentName}
    ${${contentNameLower}_SOURCE_DIR}
    ${${contentNameLower}_BINARY_DIR}
  )

  # Pass variables back to the caller. The variables passed back here
  # must match what FetchContent_GetProperties() sets when it is called
  # with just the content name.
  set(${contentNameLower}_SOURCE_DIR "${${contentNameLower}_SOURCE_DIR}" PARENT_SCOPE)
  set(${contentNameLower}_BINARY_DIR "${${contentNameLower}_BINARY_DIR}" PARENT_SCOPE)
  set(${contentNameLower}_POPULATED  True PARENT_SCOPE)

endfunction()

# Arguments are assumed to be the names of dependencies that have been
# declared previously and should be populated. It is not an error if
# any of them have already been populated (they will just be skipped in
# that case). The command is implemented as a macro so that the variables
# defined by the FetchContent_GetProperties() and FetchContent_Populate()
# calls will be available to the caller.
macro(FetchContent_MakeAvailable)

  foreach(contentName IN ITEMS ${ARGV})
    string(TOLOWER ${contentName} contentNameLower)
    FetchContent_GetProperties(${contentName})
    if(NOT ${contentNameLower}_POPULATED)
      FetchContent_Populate(${contentName})

      # Only try to call add_subdirectory() if the populated content
      # can be treated that way. Protecting the call with the check
      # allows this function to be used for projects that just want
      # to ensure the content exists, such as to provide content at
      # a known location. We check the saved details for an optional
      # SOURCE_SUBDIR which can be used in the same way as its meaning
      # for ExternalProject. It won't matter if it was passed through
      # to the ExternalProject sub-build, since it would have been
      # ignored there.
      set(__fc_srcdir "${${contentNameLower}_SOURCE_DIR}")
      __FetchContent_getSavedDetails(${contentName} contentDetails)
      if("${contentDetails}" STREQUAL "")
        message(FATAL_ERROR "No details have been set for content: ${contentName}")
      endif()
      cmake_parse_arguments(__fc_arg "" "SOURCE_SUBDIR" "" ${contentDetails})
      if(NOT "${__fc_arg_SOURCE_SUBDIR}" STREQUAL "")
        string(APPEND __fc_srcdir "/${__fc_arg_SOURCE_SUBDIR}")
      endif()

      if(EXISTS ${__fc_srcdir}/CMakeLists.txt)
        add_subdirectory(${__fc_srcdir} ${${contentNameLower}_BINARY_DIR})
      endif()

      unset(__fc_srcdir)
    endif()
  endforeach()

endmacro()
