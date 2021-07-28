CPack productbuild Generator
----------------------------

productbuild CPack generator (macOS).

Variables specific to CPack productbuild generator
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following variable is specific to installers built on Mac
macOS using ProductBuild:

.. variable:: CPACK_COMMAND_PRODUCTBUILD

 Path to the ``productbuild(1)`` command used to generate a product archive for
 the macOS Installer or Mac App Store.  This variable can be used to override
 the automatically detected command (or specify its location if the
 auto-detection fails to find it).

.. variable:: CPACK_PRODUCTBUILD_IDENTITY_NAME

 Adds a digital signature to the resulting package.


.. variable:: CPACK_PRODUCTBUILD_KEYCHAIN_PATH

 Specify a specific keychain to search for the signing identity.


.. variable:: CPACK_COMMAND_PKGBUILD

 Path to the ``pkgbuild(1)`` command used to generate an macOS component package
 on macOS.  This variable can be used to override the automatically detected
 command (or specify its location if the auto-detection fails to find it).


.. variable:: CPACK_PKGBUILD_IDENTITY_NAME

 Adds a digital signature to the resulting package.


.. variable:: CPACK_PKGBUILD_KEYCHAIN_PATH

 Specify a specific keychain to search for the signing identity.


.. variable:: CPACK_PREFLIGHT_<COMP>_SCRIPT

 Full path to a file that will be used as the ``preinstall`` script for the
 named ``<COMP>`` component's package, where ``<COMP>`` is the uppercased
 component name.  No ``preinstall`` script is added if this variable is not
 defined for a given component.


.. variable:: CPACK_POSTFLIGHT_<COMP>_SCRIPT

 Full path to a file that will be used as the ``postinstall`` script for the
 named ``<COMP>`` component's package, where ``<COMP>`` is the uppercased
 component name.  No ``postinstall`` script is added if this variable is not
 defined for a given component.

.. variable:: CPACK_PRODUCTBUILD_RESOURCES_DIR

 If specified the productbuild generator copies files from this directory
 (including subdirectories) to the ``Resources`` directory. This is done
 before the :variable:`CPACK_RESOURCE_FILE_WELCOME`,
 :variable:`CPACK_RESOURCE_FILE_README`, and
 :variable:`CPACK_RESOURCE_FILE_LICENSE` files are copied.

.. variable:: CPACK_PRODUCTBUILD_BACKGROUND

 Adds a background to Distribution XML if specified. The value contains the
 path to image in ``Resources`` directory.

.. variable:: CPACK_PRODUCTBUILD_BACKGROUND_ALIGNMENT

 Adds an ``alignment`` attribute to the background in Distribution XML.
 Refer to Apple documentation for valid values.

.. variable:: CPACK_PRODUCTBUILD_BACKGROUND_SCALING

 Adds a ``scaling`` attribute to the background in Distribution XML.
 Refer to Apple documentation for valid values.

.. variable:: CPACK_PRODUCTBUILD_BACKGROUND_MIME_TYPE

 Adds a ``mime-type`` attribute to the background in Distribution XML.
 The option contains MIME type of an image.

.. variable:: CPACK_PRODUCTBUILD_BACKGROUND_UTI

 Adds an ``uti`` attribute to the background in Distribution XML.
 The option contains UTI type of an image.

.. variable:: CPACK_PRODUCTBUILD_BACKGROUND_DARKAQUA

 Adds a background for the Dark Aqua theme to Distribution XML if
 specified. The value contains the path to image in ``Resources``
 directory.

.. variable:: CPACK_PRODUCTBUILD_BACKGROUND_DARKAQUA_ALIGNMENT

 Does the same as :variable:`CPACK_PRODUCTBUILD_BACKGROUND_ALIGNMENT` option,
 but for the dark theme.

.. variable:: CPACK_PRODUCTBUILD_BACKGROUND_DARKAQUA_SCALING

 Does the same as :variable:`CPACK_PRODUCTBUILD_BACKGROUND_SCALING` option,
 but for the dark theme.

.. variable:: CPACK_PRODUCTBUILD_BACKGROUND_DARKAQUA_MIME_TYPE

 Does the same as :variable:`CPACK_PRODUCTBUILD_BACKGROUND_MIME_TYPE` option,
 but for the dark theme.

.. variable:: CPACK_PRODUCTBUILD_BACKGROUND_DARKAQUA_UTI

 Does the same as :variable:`CPACK_PRODUCTBUILD_BACKGROUND_UTI` option,
 but for the dark theme.
