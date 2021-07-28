CPack PackageMaker Generator
----------------------------

PackageMaker CPack generator (macOS).

.. deprecated:: 3.17

  Xcode no longer distributes the PackageMaker tools.
  This CPack generator will be removed in a future version of CPack.

Variables specific to CPack PackageMaker generator
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following variable is specific to installers built on Mac
macOS using PackageMaker:

.. variable:: CPACK_OSX_PACKAGE_VERSION

 The version of macOS that the resulting PackageMaker archive should be
 compatible with. Different versions of macOS support different
 features. For example, CPack can only build component-based installers for
 macOS 10.4 or newer, and can only build installers that download
 components on-the-fly for macOS 10.5 or newer. If left blank, this value
 will be set to the minimum version of macOS that supports the requested
 features. Set this variable to some value (e.g., 10.4) only if you want to
 guarantee that your installer will work on that version of macOS, and
 don't mind missing extra features available in the installer shipping with
 later versions of macOS.

.. variable:: CPACK_PACKAGEMAKER_BACKGROUND

 Adds a background to Distribution XML if specified. The value contains the
 path to image in ``Resources`` directory.

.. variable:: CPACK_PACKAGEMAKER_BACKGROUND_ALIGNMENT

 Adds an ``alignment`` attribute to the background in Distribution XML.
 Refer to Apple documentation for valid values.

.. variable:: CPACK_PACKAGEMAKER_BACKGROUND_SCALING

 Adds a ``scaling`` attribute to the background in Distribution XML.
 Refer to Apple documentation for valid values.

.. variable:: CPACK_PACKAGEMAKER_BACKGROUND_MIME_TYPE

 Adds a ``mime-type`` attribute to the background in Distribution XML.
 The option contains MIME type of an image.

.. variable:: CPACK_PACKAGEMAKER_BACKGROUND_UTI

 Adds an ``uti`` attribute to the background in Distribution XML.
 The option contains UTI type of an image.

.. variable:: CPACK_PACKAGEMAKER_BACKGROUND_DARKAQUA

 Adds a background for the Dark Aqua theme to Distribution XML if
 specified. The value contains the path to image in ``Resources``
 directory.

.. variable:: CPACK_PACKAGEMAKER_BACKGROUND_DARKAQUA_ALIGNMENT

 Does the same as :variable:`CPACK_PACKAGEMAKER_BACKGROUND_ALIGNMENT` option,
 but for the dark theme.

.. variable:: CPACK_PACKAGEMAKER_BACKGROUND_DARKAQUA_SCALING

 Does the same as :variable:`CPACK_PACKAGEMAKER_BACKGROUND_SCALING` option,
 but for the dark theme.

.. variable:: CPACK_PACKAGEMAKER_BACKGROUND_DARKAQUA_MIME_TYPE

 Does the same as :variable:`CPACK_PACKAGEMAKER_BACKGROUND_MIME_TYPE` option,
 but for the dark theme.

.. variable:: CPACK_PACKAGEMAKER_BACKGROUND_DARKAQUA_UTI

 Does the same as :variable:`CPACK_PACKAGEMAKER_BACKGROUND_UTI` option,
 but for the dark theme.
