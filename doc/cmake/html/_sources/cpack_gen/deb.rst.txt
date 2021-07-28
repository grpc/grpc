CPack DEB Generator
-------------------

The built in (binary) CPack DEB generator (Unix only)

Variables specific to CPack Debian (DEB) generator
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The CPack DEB generator may be used to create DEB package using :module:`CPack`.
The CPack DEB generator is a :module:`CPack` generator thus it uses the
``CPACK_XXX`` variables used by :module:`CPack`.

The CPack DEB generator should work on any Linux host but it will produce
better deb package when Debian specific tools ``dpkg-xxx`` are usable on
the build system.

The CPack DEB generator has specific features which are controlled by the
specifics ``CPACK_DEBIAN_XXX`` variables.

``CPACK_DEBIAN_<COMPONENT>_XXXX`` variables may be used in order to have
**component** specific values.  Note however that ``<COMPONENT>`` refers to
the **grouping name** written in upper case. It may be either a component name
or a component GROUP name.

Here are some CPack DEB generator wiki resources that are here for historic
reasons and are no longer maintained but may still prove useful:

 - https://gitlab.kitware.com/cmake/community/-/wikis/doc/cpack/Configuration
 - https://gitlab.kitware.com/cmake/community/-/wikis/doc/cpack/PackageGenerators#deb-unix-only

List of CPack DEB generator specific variables:

.. variable:: CPACK_DEB_COMPONENT_INSTALL

 Enable component packaging for CPackDEB

 * Mandatory : NO
 * Default   : OFF

 If enabled (ON) multiple packages are generated. By default a single package
 containing files of all components is generated.

.. variable:: CPACK_DEBIAN_PACKAGE_NAME
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_NAME

 Set Package control field (variable is automatically transformed to lower
 case).

 * Mandatory : YES
 * Default   :

   - :variable:`CPACK_PACKAGE_NAME` for non-component based
     installations
   - :variable:`CPACK_DEBIAN_PACKAGE_NAME` suffixed with -<COMPONENT>
     for component-based installations.

 See https://www.debian.org/doc/debian-policy/ch-controlfields.html#s-f-Source

.. variable:: CPACK_DEBIAN_FILE_NAME
              CPACK_DEBIAN_<COMPONENT>_FILE_NAME

 Package file name.

 * Mandatory : YES
 * Default   : ``<CPACK_PACKAGE_FILE_NAME>[-<component>].deb``

 This may be set to ``DEB-DEFAULT`` to allow the CPack DEB generator to generate
 package file name by itself in deb format::

   <PackageName>_<VersionNumber>-<DebianRevisionNumber>_<DebianArchitecture>.deb

 Alternatively provided package file name must end
 with either ``.deb`` or ``.ipk`` suffix.

 .. note::

   Preferred setting of this variable is ``DEB-DEFAULT`` but for backward
   compatibility with the CPack DEB generator in CMake prior to version 3.6 this
   feature is disabled by default.

 .. note::

   By using non default filenames duplicate names may occur. Duplicate files
   get overwritten and it is up to the packager to set the variables in a
   manner that will prevent such errors.

.. variable:: CPACK_DEBIAN_PACKAGE_EPOCH

 The Debian package epoch

 * Mandatory : No
 * Default   : -

 Optional number that should be incremented when changing versioning schemas
 or fixing mistakes in the version numbers of older packages.

.. variable:: CPACK_DEBIAN_PACKAGE_VERSION

 The Debian package version

 * Mandatory : YES
 * Default   : :variable:`CPACK_PACKAGE_VERSION`

 This variable may contain only alphanumerics (A-Za-z0-9) and the characters
 . + - ~ (full stop, plus, hyphen, tilde) and should start with a digit. If
 :variable:`CPACK_DEBIAN_PACKAGE_RELEASE` is not set then hyphens are not
 allowed.

 .. note::

   For backward compatibility with CMake 3.9 and lower a failed test of this
   variable's content is not a hard error when both
   :variable:`CPACK_DEBIAN_PACKAGE_RELEASE` and
   :variable:`CPACK_DEBIAN_PACKAGE_EPOCH` variables are not set. An author
   warning is reported instead.

.. variable:: CPACK_DEBIAN_PACKAGE_RELEASE

 The Debian package release - Debian revision number.

 * Mandatory : No
 * Default   : -

 This is the numbering of the DEB package itself, i.e. the version of the
 packaging and not the version of the content (see
 :variable:`CPACK_DEBIAN_PACKAGE_VERSION`). One may change the default value
 if the previous packaging was buggy and/or you want to put here a fancy Linux
 distro specific numbering.

.. variable:: CPACK_DEBIAN_PACKAGE_ARCHITECTURE
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_ARCHITECTURE

 The Debian package architecture

 * Mandatory : YES
 * Default   : Output of ``dpkg --print-architecture`` (or ``i386``
   if ``dpkg`` is not found)

.. variable:: CPACK_DEBIAN_PACKAGE_DEPENDS
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_DEPENDS

 Sets the Debian dependencies of this package.

 * Mandatory : NO
 * Default   :

   - An empty string for non-component based installations
   - :variable:`CPACK_DEBIAN_PACKAGE_DEPENDS` for component-based
     installations.

 .. note::

   If :variable:`CPACK_DEBIAN_PACKAGE_SHLIBDEPS` or
   more specifically :variable:`CPACK_DEBIAN_<COMPONENT>_PACKAGE_SHLIBDEPS`
   is set for this component, the discovered dependencies will be appended
   to :variable:`CPACK_DEBIAN_<COMPONENT>_PACKAGE_DEPENDS` instead of
   :variable:`CPACK_DEBIAN_PACKAGE_DEPENDS`. If
   :variable:`CPACK_DEBIAN_<COMPONENT>_PACKAGE_DEPENDS` is an empty string,
   only the automatically discovered dependencies will be set for this
   component.

 Example::

   set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.3.1-6), libc6 (< 2.4)")

.. variable:: CPACK_DEBIAN_ENABLE_COMPONENT_DEPENDS

 Sets inter component dependencies if listed with
 :variable:`CPACK_COMPONENT_<compName>_DEPENDS` variables.

 * Mandatory : NO
 * Default   : -

.. variable:: CPACK_DEBIAN_PACKAGE_MAINTAINER

 The Debian package maintainer

 * Mandatory : YES
 * Default   : ``CPACK_PACKAGE_CONTACT``

.. variable:: CPACK_DEBIAN_PACKAGE_DESCRIPTION
              CPACK_DEBIAN_<COMPONENT>_DESCRIPTION

 The Debian package description

 * Mandatory : YES
 * Default   :

   - :variable:`CPACK_DEBIAN_<COMPONENT>_DESCRIPTION` (component
     based installers only) if set, or :variable:`CPACK_DEBIAN_PACKAGE_DESCRIPTION` if set, or
   - :variable:`CPACK_COMPONENT_<compName>_DESCRIPTION` (component
     based installers only) if set, or :variable:`CPACK_PACKAGE_DESCRIPTION` if set, or
   - content of the file specified in :variable:`CPACK_PACKAGE_DESCRIPTION_FILE` if set

 If after that description is not set, :variable:`CPACK_PACKAGE_DESCRIPTION_SUMMARY` going to be
 used if set. Otherwise, :variable:`CPACK_PACKAGE_DESCRIPTION_SUMMARY` will be added as the first
 line of description as defined in `Debian Policy Manual`_.

.. _Debian Policy Manual: https://www.debian.org/doc/debian-policy/ch-controlfields.html#description

.. variable:: CPACK_DEBIAN_PACKAGE_SECTION
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_SECTION

 Set Section control field e.g. admin, devel, doc, ...

 * Mandatory : YES
 * Default   : "devel"

 See https://www.debian.org/doc/debian-policy/ch-archive.html#s-subsections

.. variable:: CPACK_DEBIAN_ARCHIVE_TYPE

 The archive format used for creating the Debian package.

 * Mandatory : YES
 * Default   : "gnutar"

 Possible value is:

 - gnutar

 .. note::

   This variable previously defaulted to the ``paxr`` value, but ``dpkg``
   has never supported that tar format. For backwards compatibility the
   ``paxr`` value will be mapped to ``gnutar`` and a deprecation message
   will be emitted.

.. variable:: CPACK_DEBIAN_COMPRESSION_TYPE

 The compression used for creating the Debian package.

 * Mandatory : YES
 * Default   : "gzip"

 Possible values are:

 - lzma
 - xz
 - bzip2
 - gzip

.. variable:: CPACK_DEBIAN_PACKAGE_PRIORITY
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_PRIORITY

 Set Priority control field e.g. required, important, standard, optional,
 extra

 * Mandatory : YES
 * Default   : "optional"

 See https://www.debian.org/doc/debian-policy/ch-archive.html#s-priorities

.. variable:: CPACK_DEBIAN_PACKAGE_HOMEPAGE

 The URL of the web site for this package, preferably (when applicable) the
 site from which the original source can be obtained and any additional
 upstream documentation or information may be found.

 * Mandatory : NO
 * Default   : :variable:`CMAKE_PROJECT_HOMEPAGE_URL`

 .. note::

   The content of this field is a simple URL without any surrounding
   characters such as <>.

.. variable:: CPACK_DEBIAN_PACKAGE_SHLIBDEPS
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_SHLIBDEPS

 May be set to ON in order to use ``dpkg-shlibdeps`` to generate
 better package dependency list.

 * Mandatory : NO
 * Default   :

   - :variable:`CPACK_DEBIAN_PACKAGE_SHLIBDEPS` if set or
   - OFF

 .. note::

   You may need set :variable:`CMAKE_INSTALL_RPATH` to an appropriate value
   if you use this feature, because if you don't ``dpkg-shlibdeps``
   may fail to find your own shared libs.
   See https://gitlab.kitware.com/cmake/community/-/wikis/doc/cmake/RPATH-handling

.. variable:: CPACK_DEBIAN_PACKAGE_DEBUG

 May be set when invoking cpack in order to trace debug information
 during the CPack DEB generator run.

 * Mandatory : NO
 * Default   : -

.. variable:: CPACK_DEBIAN_PACKAGE_PREDEPENDS
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_PREDEPENDS

 Sets the `Pre-Depends` field of the Debian package.
 Like :variable:`Depends <CPACK_DEBIAN_PACKAGE_DEPENDS>`, except that it
 also forces ``dpkg`` to complete installation of the packages named
 before even starting the installation of the package which declares the
 pre-dependency.

 * Mandatory : NO
 * Default   :

   - An empty string for non-component based installations
   - :variable:`CPACK_DEBIAN_PACKAGE_PREDEPENDS` for component-based
     installations.

 See http://www.debian.org/doc/debian-policy/ch-relationships.html#s-binarydeps

.. variable:: CPACK_DEBIAN_PACKAGE_ENHANCES
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_ENHANCES

 Sets the `Enhances` field of the Debian package.
 Similar to :variable:`Suggests <CPACK_DEBIAN_PACKAGE_SUGGESTS>` but works
 in the opposite direction: declares that a package can enhance the
 functionality of another package.

 * Mandatory : NO
 * Default   :

   - An empty string for non-component based installations
   - :variable:`CPACK_DEBIAN_PACKAGE_ENHANCES` for component-based
     installations.

 See http://www.debian.org/doc/debian-policy/ch-relationships.html#s-binarydeps

.. variable:: CPACK_DEBIAN_PACKAGE_BREAKS
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_BREAKS

 Sets the `Breaks` field of the Debian package.
 When a binary package (P) declares that it breaks other packages (B),
 ``dpkg`` will not allow the package (P) which declares `Breaks` be
 **unpacked** unless the packages that will be broken (B) are deconfigured
 first.
 As long as the package (P) is configured, the previously deconfigured
 packages (B) cannot be reconfigured again.

 * Mandatory : NO
 * Default   :

   - An empty string for non-component based installations
   - :variable:`CPACK_DEBIAN_PACKAGE_BREAKS` for component-based
     installations.

 See https://www.debian.org/doc/debian-policy/ch-relationships.html#s-breaks

.. variable:: CPACK_DEBIAN_PACKAGE_CONFLICTS
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_CONFLICTS

 Sets the `Conflicts` field of the Debian package.
 When one binary package declares a conflict with another using a `Conflicts`
 field, ``dpkg`` will not allow them to be unpacked on the system at
 the same time.

 * Mandatory : NO
 * Default   :

   - An empty string for non-component based installations
   - :variable:`CPACK_DEBIAN_PACKAGE_CONFLICTS` for component-based
     installations.

 See https://www.debian.org/doc/debian-policy/ch-relationships.html#s-conflicts

 .. note::

   This is a stronger restriction than
   :variable:`Breaks <CPACK_DEBIAN_PACKAGE_BREAKS>`, which prevents the
   broken package from being configured while the breaking package is in
   the "Unpacked" state but allows both packages to be unpacked at the same
   time.

.. variable:: CPACK_DEBIAN_PACKAGE_PROVIDES
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_PROVIDES

 Sets the `Provides` field of the Debian package.
 A virtual package is one which appears in the `Provides` control field of
 another package.

 * Mandatory : NO
 * Default   :

   - An empty string for non-component based installations
   - :variable:`CPACK_DEBIAN_PACKAGE_PROVIDES` for component-based
     installations.

 See https://www.debian.org/doc/debian-policy/ch-relationships.html#s-virtual

.. variable:: CPACK_DEBIAN_PACKAGE_REPLACES
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_REPLACES

 Sets the `Replaces` field of the Debian package.
 Packages can declare in their control file that they should overwrite
 files in certain other packages, or completely replace other packages.

 * Mandatory : NO
 * Default   :

   - An empty string for non-component based installations
   - :variable:`CPACK_DEBIAN_PACKAGE_REPLACES` for component-based
     installations.

 See http://www.debian.org/doc/debian-policy/ch-relationships.html#s-binarydeps

.. variable:: CPACK_DEBIAN_PACKAGE_RECOMMENDS
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_RECOMMENDS

 Sets the `Recommends` field of the Debian package.
 Allows packages to declare a strong, but not absolute, dependency on other
 packages.

 * Mandatory : NO
 * Default   :

   - An empty string for non-component based installations
   - :variable:`CPACK_DEBIAN_PACKAGE_RECOMMENDS` for component-based
     installations.

 See http://www.debian.org/doc/debian-policy/ch-relationships.html#s-binarydeps

.. variable:: CPACK_DEBIAN_PACKAGE_SUGGESTS
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_SUGGESTS

 Sets the `Suggests` field of the Debian package.
 Allows packages to declare a suggested package install grouping.

 * Mandatory : NO
 * Default   :

   - An empty string for non-component based installations
   - :variable:`CPACK_DEBIAN_PACKAGE_SUGGESTS` for component-based
     installations.

 See http://www.debian.org/doc/debian-policy/ch-relationships.html#s-binarydeps

.. variable:: CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS

 * Mandatory : NO
 * Default   : OFF

 Allows to generate shlibs control file automatically. Compatibility is defined by
 :variable:`CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS_POLICY` variable value.

 .. note::

   Libraries are only considered if they have both library name and version
   set. This can be done by setting SOVERSION property with
   :command:`set_target_properties` command.

.. variable:: CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS_POLICY

 Compatibility policy for auto-generated shlibs control file.

 * Mandatory : NO
 * Default   : "="

 Defines compatibility policy for auto-generated shlibs control file.
 Possible values: "=", ">="

 See https://www.debian.org/doc/debian-policy/ch-sharedlibs.html#s-sharedlibs-shlibdeps

.. variable:: CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_CONTROL_EXTRA

 This variable allow advanced user to add custom script to the
 control.tar.gz.
 Typical usage is for conffiles, postinst, postrm, prerm.

 * Mandatory : NO
 * Default   : -

 Usage::

  set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
      "${CMAKE_CURRENT_SOURCE_DIR}/prerm;${CMAKE_CURRENT_SOURCE_DIR}/postrm")

 .. note::

   The original permissions of the files will be used in the final
   package unless the variable
   :variable:`CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION` is set.
   In particular, the scripts should have the proper executable
   flag prior to the generation of the package.

.. variable:: CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_CONTROL_STRICT_PERMISSION

 This variable indicates if the Debian policy on control files should be
 strictly followed.

 * Mandatory : NO
 * Default   : FALSE

 Usage::

  set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)

 .. note::

   This overrides the permissions on the original files, following the rules
   set by Debian policy
   https://www.debian.org/doc/debian-policy/ch-files.html#s-permissions-owners

.. variable:: CPACK_DEBIAN_PACKAGE_SOURCE
              CPACK_DEBIAN_<COMPONENT>_PACKAGE_SOURCE

 Sets the ``Source`` field of the binary Debian package.
 When the binary package name is not the same as the source package name
 (in particular when several components/binaries are generated from one
 source) the source from which the binary has been generated should be
 indicated with the field ``Source``.

 * Mandatory : NO
 * Default   :

   - An empty string for non-component based installations
   - :variable:`CPACK_DEBIAN_PACKAGE_SOURCE` for component-based
     installations.

 See https://www.debian.org/doc/debian-policy/ch-controlfields.html#s-f-Source

 .. note::

   This value is not interpreted. It is possible to pass an optional
   revision number of the referenced source package as well.

Packaging of debug information
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Dbgsym packages contain debug symbols for debugging packaged binaries.

Dbgsym packaging has its own set of variables:

.. variable:: CPACK_DEBIAN_DEBUGINFO_PACKAGE
              CPACK_DEBIAN_<component>_DEBUGINFO_PACKAGE

 Enable generation of dbgsym .ddeb package(s).

 * Mandatory : NO
 * Default   : OFF

.. note::

 Binaries must contain debug symbols before packaging so use either ``Debug``
 or ``RelWithDebInfo`` for :variable:`CMAKE_BUILD_TYPE` variable value.

Building Debian packages on Windows
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To communicate UNIX file permissions from the install stage
to the CPack DEB generator the "cmake_mode_t" NTFS
alternate data stream (ADT) is used.

When a filesystem without ADT support is used only owner read/write
permissions can be preserved.

Reproducible packages
^^^^^^^^^^^^^^^^^^^^^

The environment variable ``SOURCE_DATE_EPOCH`` may be set to a UNIX
timestamp, defined as the number of seconds, excluding leap seconds,
since 01 Jan 1970 00:00:00 UTC.  If set, the CPack DEB generator will
use its value for timestamps in the package.
