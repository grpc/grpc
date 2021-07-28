CPack NuGet Generator
---------------------

When build a NuGet package there is no direct way to control an output
filename due a lack of the corresponding CLI option of NuGet, so there
is no ``CPACK_NUGET_PACKAGE_FILENAME`` variable. To form the output filename
NuGet uses the package name and the version according to its built-in rules.

Also, be aware that including a top level directory
(``CPACK_INCLUDE_TOPLEVEL_DIRECTORY``) is ignored by this generator.


Variables specific to CPack NuGet generator
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The CPack NuGet generator may be used to create NuGet packages using
:module:`CPack`. The CPack NuGet generator is a :module:`CPack` generator thus
it uses the ``CPACK_XXX`` variables used by :module:`CPack`.

The CPack NuGet generator has specific features which are controlled by the
specifics ``CPACK_NUGET_XXX`` variables. In the "one per group" mode
(see :variable:`CPACK_COMPONENTS_GROUPING`), ``<compName>`` placeholder
in the variables below would contain a group name (uppercased and turned into
a "C" identifier).

List of CPack NuGet generator specific variables:

.. variable:: CPACK_NUGET_COMPONENT_INSTALL

 Enable component packaging for CPack NuGet generator

 * Mandatory : NO
 * Default   : OFF

.. variable:: CPACK_NUGET_PACKAGE_NAME
              CPACK_NUGET_<compName>_PACKAGE_NAME

 The NUGET package name.

 * Mandatory : YES
 * Default   : :variable:`CPACK_PACKAGE_NAME`

.. variable:: CPACK_NUGET_PACKAGE_VERSION
              CPACK_NUGET_<compName>_PACKAGE_VERSION

 The NuGet package version.

 * Mandatory : YES
 * Default   : :variable:`CPACK_PACKAGE_VERSION`

.. variable:: CPACK_NUGET_PACKAGE_DESCRIPTION
              CPACK_NUGET_<compName>_PACKAGE_DESCRIPTION

 A long description of the package for UI display.

 * Mandatory : YES
 * Default   :
    - :variable:`CPACK_COMPONENT_<compName>_DESCRIPTION`,
    - ``CPACK_COMPONENT_GROUP_<groupName>_DESCRIPTION``,
    - :variable:`CPACK_PACKAGE_DESCRIPTION`

.. variable:: CPACK_NUGET_PACKAGE_AUTHORS
              CPACK_NUGET_<compName>_PACKAGE_AUTHORS

 A comma-separated list of packages authors, matching the profile names
 on nuget.org_. These are displayed in the NuGet Gallery on
 nuget.org_ and are used to cross-reference packages by the same
 authors.

 * Mandatory : YES
 * Default   : :variable:`CPACK_PACKAGE_VENDOR`

.. variable:: CPACK_NUGET_PACKAGE_TITLE
              CPACK_NUGET_<compName>_PACKAGE_TITLE

 A human-friendly title of the package, typically used in UI displays
 as on nuget.org_ and the Package Manager in Visual Studio. If not
 specified, the package ID is used.

 * Mandatory : NO
 * Default   :
    - :variable:`CPACK_COMPONENT_<compName>_DISPLAY_NAME`,
    - ``CPACK_COMPONENT_GROUP_<groupName>_DISPLAY_NAME``

.. variable:: CPACK_NUGET_PACKAGE_OWNERS
              CPACK_NUGET_<compName>_PACKAGE_OWNERS

 A comma-separated list of the package creators using profile names
 on nuget.org_. This is often the same list as in authors,
 and is ignored when uploading the package to nuget.org_.

 * Mandatory : NO
 * Default   : -

.. variable:: CPACK_NUGET_PACKAGE_HOMEPAGE_URL
              CPACK_NUGET_<compName>_PACKAGE_HOMEPAGE_URL

 A URL for the package's home page, often shown in UI displays as well
 as nuget.org_.

 * Mandatory : NO
 * Default   : :variable:`CPACK_PACKAGE_HOMEPAGE_URL`

.. variable:: CPACK_NUGET_PACKAGE_LICENSEURL
              CPACK_NUGET_<compName>_PACKAGE_LICENSEURL

 A URL for the package's license, often shown in UI displays as well
 as nuget.org_.

 * Mandatory : NO
 * Default   : -

.. variable:: CPACK_NUGET_PACKAGE_ICONURL
              CPACK_NUGET_<compName>_PACKAGE_ICONURL

 A URL for a 64x64 image with transparency background to use as the
 icon for the package in UI display.

 * Mandatory : NO
 * Default   : -

.. variable:: CPACK_NUGET_PACKAGE_DESCRIPTION_SUMMARY
              CPACK_NUGET_<compName>_PACKAGE_DESCRIPTION_SUMMARY

 A short description of the package for UI display. If omitted, a
 truncated version of description is used.

 * Mandatory : NO
 * Default   : :variable:`CPACK_PACKAGE_DESCRIPTION_SUMMARY`

.. variable:: CPACK_NUGET_PACKAGE_RELEASE_NOTES
              CPACK_NUGET_<compName>_PACKAGE_RELEASE_NOTES

 A description of the changes made in this release of the package,
 often used in UI like the Updates tab of the Visual Studio Package
 Manager in place of the package description.

 * Mandatory : NO
 * Default   : -

.. variable:: CPACK_NUGET_PACKAGE_COPYRIGHT
              CPACK_NUGET_<compName>_PACKAGE_COPYRIGHT

 Copyright details for the package.

 * Mandatory : NO
 * Default   : -

.. variable:: CPACK_NUGET_PACKAGE_TAGS
              CPACK_NUGET_<compName>_PACKAGE_TAGS

 A space-delimited list of tags and keywords that describe the
 package and aid discoverability of packages through search and
 filtering.

 * Mandatory : NO
 * Default   : -

.. variable:: CPACK_NUGET_PACKAGE_DEPENDENCIES
              CPACK_NUGET_<compName>_PACKAGE_DEPENDENCIES

 A list of package dependencies.

 * Mandatory : NO
 * Default   : -

.. variable:: CPACK_NUGET_PACKAGE_DEPENDENCIES_<dependency>_VERSION
              CPACK_NUGET_<compName>_PACKAGE_DEPENDENCIES_<dependency>_VERSION

 A `version specification`_ for the particular dependency, where
 ``<dependency>`` is an item of the dependency list (see above)
 transformed with ``MAKE_C_IDENTIFIER`` function of :command:`string`
 command.

 * Mandatory : NO
 * Default   : -

.. variable:: CPACK_NUGET_PACKAGE_DEBUG

 Enable debug messages while executing CPack NuGet generator.

 * Mandatory : NO
 * Default   : OFF


.. _nuget.org: http://nuget.org
.. _version specification: https://docs.microsoft.com/en-us/nuget/reference/package-versioning#version-ranges-and-wildcards

.. NuGet spec docs https://docs.microsoft.com/en-us/nuget/reference/nuspec
