CPack WIX Generator
-------------------

CPack WIX generator specific options

Variables specific to CPack WIX generator
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following variables are specific to the installers built on
Windows using WiX.

.. variable:: CPACK_WIX_UPGRADE_GUID

 Upgrade GUID (``Product/@UpgradeCode``)

 Will be automatically generated unless explicitly provided.

 It should be explicitly set to a constant generated globally unique
 identifier (GUID) to allow your installers to replace existing
 installations that use the same GUID.

 You may for example explicitly set this variable in your
 CMakeLists.txt to the value that has been generated per default.  You
 should not use GUIDs that you did not generate yourself or which may
 belong to other projects.

 A GUID shall have the following fixed length syntax::

  XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX

 (each X represents an uppercase hexadecimal digit)

.. variable:: CPACK_WIX_PRODUCT_GUID

 Product GUID (``Product/@Id``)

 Will be automatically generated unless explicitly provided.

 If explicitly provided this will set the Product Id of your installer.

 The installer will abort if it detects a pre-existing installation that
 uses the same GUID.

 The GUID shall use the syntax described for CPACK_WIX_UPGRADE_GUID.

.. variable:: CPACK_WIX_LICENSE_RTF

 RTF License File

 If CPACK_RESOURCE_FILE_LICENSE has an .rtf extension it is used as-is.

 If CPACK_RESOURCE_FILE_LICENSE has an .txt extension it is implicitly
 converted to RTF by the WIX Generator.
 The expected encoding of the .txt file is UTF-8.

 With CPACK_WIX_LICENSE_RTF you can override the license file used by the
 WIX Generator in case CPACK_RESOURCE_FILE_LICENSE is in an unsupported
 format or the .txt -> .rtf conversion does not work as expected.

.. variable:: CPACK_WIX_PRODUCT_ICON

 The Icon shown next to the program name in Add/Remove programs.

 If set, this icon is used in place of the default icon.

.. variable:: CPACK_WIX_UI_REF

 This variable allows you to override the Id of the ``<UIRef>`` element
 in the WiX template.

 The default is ``WixUI_InstallDir`` in case no CPack components have
 been defined and ``WixUI_FeatureTree`` otherwise.

.. variable:: CPACK_WIX_UI_BANNER

 The bitmap will appear at the top of all installer pages other than the
 welcome and completion dialogs.

 If set, this image will replace the default banner image.

 This image must be 493 by 58 pixels.

.. variable:: CPACK_WIX_UI_DIALOG

 Background bitmap used on the welcome and completion dialogs.

 If this variable is set, the installer will replace the default dialog
 image.

 This image must be 493 by 312 pixels.

.. variable:: CPACK_WIX_PROGRAM_MENU_FOLDER

 Start menu folder name for launcher.

 If this variable is not set, it will be initialized with CPACK_PACKAGE_NAME

 If this variable is set to ``.``, then application shortcuts will be
 created directly in the start menu and the uninstaller shortcut will be
 omitted.

.. variable:: CPACK_WIX_CULTURES

 Language(s) of the installer

 Languages are compiled into the WixUI extension library.  To use them,
 simply provide the name of the culture.  If you specify more than one
 culture identifier in a comma or semicolon delimited list, the first one
 that is found will be used.  You can find a list of supported languages at:
 http://wix.sourceforge.net/manual-wix3/WixUI_localization.htm

.. variable:: CPACK_WIX_TEMPLATE

 Template file for WiX generation

 If this variable is set, the specified template will be used to generate
 the WiX wxs file.  This should be used if further customization of the
 output is required.

 If this variable is not set, the default MSI template included with CMake
 will be used.

.. variable:: CPACK_WIX_PATCH_FILE

 Optional list of XML files with fragments to be inserted into
 generated WiX sources

 This optional variable can be used to specify an XML file that the
 WIX generator will use to inject fragments into its generated
 source files.

 Patch files understood by the CPack WIX generator
 roughly follow this RELAX NG compact schema:

 .. code-block:: none

    start = CPackWiXPatch

    CPackWiXPatch = element CPackWiXPatch { CPackWiXFragment* }

    CPackWiXFragment = element CPackWiXFragment
    {
        attribute Id { string },
        fragmentContent*
    }

    fragmentContent = element * - CPackWiXFragment
    {
        (attribute * { text } | text | fragmentContent)*
    }

 Currently fragments can be injected into most
 Component, File, Directory and Feature elements.

 The following additional special Ids can be used:

 * ``#PRODUCT`` for the ``<Product>`` element.
 * ``#PRODUCTFEATURE`` for the root ``<Feature>`` element.

 The following example illustrates how this works.

 Given that the WIX generator creates the following XML element:

 .. code-block:: xml

    <Component Id="CM_CP_applications.bin.my_libapp.exe" Guid="*"/>

 The following XML patch file may be used to inject an Environment element
 into it:

 .. code-block:: xml

    <CPackWiXPatch>
      <CPackWiXFragment Id="CM_CP_applications.bin.my_libapp.exe">
        <Environment Id="MyEnvironment" Action="set"
          Name="MyVariableName" Value="MyVariableValue"/>
      </CPackWiXFragment>
    </CPackWiXPatch>

.. variable:: CPACK_WIX_EXTRA_SOURCES

 Extra WiX source files

 This variable provides an optional list of extra WiX source files (.wxs)
 that should be compiled and linked.  The full path to source files is
 required.

.. variable:: CPACK_WIX_EXTRA_OBJECTS

 Extra WiX object files or libraries

 This variable provides an optional list of extra WiX object (.wixobj)
 and/or WiX library (.wixlib) files.  The full path to objects and libraries
 is required.

.. variable:: CPACK_WIX_EXTENSIONS

 This variable provides a list of additional extensions for the WiX
 tools light and candle.

.. variable:: CPACK_WIX_<TOOL>_EXTENSIONS

 This is the tool specific version of CPACK_WIX_EXTENSIONS.
 ``<TOOL>`` can be either LIGHT or CANDLE.

.. variable:: CPACK_WIX_<TOOL>_EXTRA_FLAGS

 This list variable allows you to pass additional
 flags to the WiX tool ``<TOOL>``.

 Use it at your own risk.
 Future versions of CPack may generate flags which may be in conflict
 with your own flags.

 ``<TOOL>`` can be either LIGHT or CANDLE.

.. variable:: CPACK_WIX_CMAKE_PACKAGE_REGISTRY

 If this variable is set the generated installer will create
 an entry in the windows registry key
 ``HKEY_LOCAL_MACHINE\Software\Kitware\CMake\Packages\<PackageName>``
 The value for ``<PackageName>`` is provided by this variable.

 Assuming you also install a CMake configuration file this will
 allow other CMake projects to find your package with
 the :command:`find_package` command.

.. variable:: CPACK_WIX_PROPERTY_<PROPERTY>

 This variable can be used to provide a value for
 the Windows Installer property ``<PROPERTY>``

 The following list contains some example properties that can be used to
 customize information under
 "Programs and Features" (also known as "Add or Remove Programs")

 * ARPCOMMENTS - Comments
 * ARPHELPLINK - Help and support information URL
 * ARPURLINFOABOUT - General information URL
 * ARPURLUPDATEINFO - Update information URL
 * ARPHELPTELEPHONE - Help and support telephone number
 * ARPSIZE - Size (in kilobytes) of the application

.. variable:: CPACK_WIX_ROOT_FEATURE_TITLE

 Sets the name of the root install feature in the WIX installer. Same as
 CPACK_COMPONENT_<compName>_DISPLAY_NAME for components.

.. variable:: CPACK_WIX_ROOT_FEATURE_DESCRIPTION

 Sets the description of the root install feature in the WIX installer. Same as
 CPACK_COMPONENT_<compName>_DESCRIPTION for components.

.. variable:: CPACK_WIX_SKIP_PROGRAM_FOLDER

 If this variable is set to true, the default install location
 of the generated package will be CPACK_PACKAGE_INSTALL_DIRECTORY directly.
 The install location will not be located relatively below
 ProgramFiles or ProgramFiles64.

  .. note::
    Installers created with this feature do not take differences
    between the system on which the installer is created
    and the system on which the installer might be used into account.

    It is therefore possible that the installer e.g. might try to install
    onto a drive that is unavailable or unintended or a path that does not
    follow the localization or convention of the system on which the
    installation is performed.

.. variable:: CPACK_WIX_ROOT_FOLDER_ID

 This variable allows specification of a custom root folder ID.
 The generator specific ``<64>`` token can be used for
 folder IDs that come in 32-bit and 64-bit variants.
 In 32-bit builds the token will expand empty while in 64-bit builds
 it will expand to ``64``.

 When unset generated installers will default installing to
 ``ProgramFiles<64>Folder``.

.. variable:: CPACK_WIX_ROOT

 This variable can optionally be set to the root directory
 of a custom WiX Toolset installation.

 When unspecified CPack will try to locate a WiX Toolset
 installation via the ``WIX`` environment variable instead.

.. variable:: CPACK_WIX_CUSTOM_XMLNS

 This variable provides a list of custom namespace declarations that are necessary
 for using WiX extensions. Each declaration should be in the form name=url, where
 name is the plain namespace without the usual xmlns: prefix and url is an unquoted
 namespace url. A list of commonly known WiX schemata can be found here:
 https://wixtoolset.org/documentation/manual/v3/xsd/
