.. cmake-manual-description: CMake Properties Reference

cmake-properties(7)
*******************

.. only:: html

   .. contents::

.. _`Global Properties`:

Properties of Global Scope
==========================

.. toctree::
   :maxdepth: 1

   /prop_gbl/ALLOW_DUPLICATE_CUSTOM_TARGETS
   /prop_gbl/AUTOGEN_SOURCE_GROUP
   /prop_gbl/AUTOGEN_TARGETS_FOLDER
   /prop_gbl/AUTOMOC_SOURCE_GROUP
   /prop_gbl/AUTOMOC_TARGETS_FOLDER
   /prop_gbl/AUTORCC_SOURCE_GROUP
   /prop_gbl/CMAKE_C_KNOWN_FEATURES
   /prop_gbl/CMAKE_CUDA_KNOWN_FEATURES
   /prop_gbl/CMAKE_CXX_KNOWN_FEATURES
   /prop_gbl/CMAKE_ROLE
   /prop_gbl/DEBUG_CONFIGURATIONS
   /prop_gbl/DISABLED_FEATURES
   /prop_gbl/ECLIPSE_EXTRA_CPROJECT_CONTENTS
   /prop_gbl/ECLIPSE_EXTRA_NATURES
   /prop_gbl/ENABLED_FEATURES
   /prop_gbl/ENABLED_LANGUAGES
   /prop_gbl/FIND_LIBRARY_USE_LIB32_PATHS
   /prop_gbl/FIND_LIBRARY_USE_LIB64_PATHS
   /prop_gbl/FIND_LIBRARY_USE_LIBX32_PATHS
   /prop_gbl/FIND_LIBRARY_USE_OPENBSD_VERSIONING
   /prop_gbl/GENERATOR_IS_MULTI_CONFIG
   /prop_gbl/GLOBAL_DEPENDS_DEBUG_MODE
   /prop_gbl/GLOBAL_DEPENDS_NO_CYCLES
   /prop_gbl/IN_TRY_COMPILE
   /prop_gbl/JOB_POOLS
   /prop_gbl/PACKAGES_FOUND
   /prop_gbl/PACKAGES_NOT_FOUND
   /prop_gbl/PREDEFINED_TARGETS_FOLDER
   /prop_gbl/REPORT_UNDEFINED_PROPERTIES
   /prop_gbl/RULE_LAUNCH_COMPILE
   /prop_gbl/RULE_LAUNCH_CUSTOM
   /prop_gbl/RULE_LAUNCH_LINK
   /prop_gbl/RULE_MESSAGES
   /prop_gbl/TARGET_ARCHIVES_MAY_BE_SHARED_LIBS
   /prop_gbl/TARGET_MESSAGES
   /prop_gbl/TARGET_SUPPORTS_SHARED_LIBS
   /prop_gbl/USE_FOLDERS
   /prop_gbl/XCODE_EMIT_EFFECTIVE_PLATFORM_NAME

.. _`Directory Properties`:

Properties on Directories
=========================

.. toctree::
   :maxdepth: 1

   /prop_dir/ADDITIONAL_CLEAN_FILES
   /prop_dir/BINARY_DIR
   /prop_dir/BUILDSYSTEM_TARGETS
   /prop_dir/CACHE_VARIABLES
   /prop_dir/CLEAN_NO_CUSTOM
   /prop_dir/CMAKE_CONFIGURE_DEPENDS
   /prop_dir/COMPILE_DEFINITIONS
   /prop_dir/COMPILE_OPTIONS
   /prop_dir/DEFINITIONS
   /prop_dir/EXCLUDE_FROM_ALL
   /prop_dir/IMPLICIT_DEPENDS_INCLUDE_TRANSFORM
   /prop_dir/INCLUDE_DIRECTORIES
   /prop_dir/INCLUDE_REGULAR_EXPRESSION
   /prop_dir/INTERPROCEDURAL_OPTIMIZATION
   /prop_dir/INTERPROCEDURAL_OPTIMIZATION_CONFIG
   /prop_dir/LABELS
   /prop_dir/LINK_DIRECTORIES
   /prop_dir/LINK_OPTIONS
   /prop_dir/LISTFILE_STACK
   /prop_dir/MACROS
   /prop_dir/PARENT_DIRECTORY
   /prop_dir/RULE_LAUNCH_COMPILE
   /prop_dir/RULE_LAUNCH_CUSTOM
   /prop_dir/RULE_LAUNCH_LINK
   /prop_dir/SOURCE_DIR
   /prop_dir/SUBDIRECTORIES
   /prop_dir/TESTS
   /prop_dir/TEST_INCLUDE_FILES
   /prop_dir/VARIABLES
   /prop_dir/VS_GLOBAL_SECTION_POST_section
   /prop_dir/VS_GLOBAL_SECTION_PRE_section
   /prop_dir/VS_STARTUP_PROJECT

.. _`Target Properties`:

Properties on Targets
=====================

.. toctree::
   :maxdepth: 1

   /prop_tgt/ADDITIONAL_CLEAN_FILES
   /prop_tgt/AIX_EXPORT_ALL_SYMBOLS
   /prop_tgt/ALIAS_GLOBAL
   /prop_tgt/ALIASED_TARGET
   /prop_tgt/ANDROID_ANT_ADDITIONAL_OPTIONS
   /prop_tgt/ANDROID_API
   /prop_tgt/ANDROID_API_MIN
   /prop_tgt/ANDROID_ARCH
   /prop_tgt/ANDROID_ASSETS_DIRECTORIES
   /prop_tgt/ANDROID_GUI
   /prop_tgt/ANDROID_JAR_DEPENDENCIES
   /prop_tgt/ANDROID_JAR_DIRECTORIES
   /prop_tgt/ANDROID_JAVA_SOURCE_DIR
   /prop_tgt/ANDROID_NATIVE_LIB_DEPENDENCIES
   /prop_tgt/ANDROID_NATIVE_LIB_DIRECTORIES
   /prop_tgt/ANDROID_PROCESS_MAX
   /prop_tgt/ANDROID_PROGUARD
   /prop_tgt/ANDROID_PROGUARD_CONFIG_PATH
   /prop_tgt/ANDROID_SECURE_PROPS_PATH
   /prop_tgt/ANDROID_SKIP_ANT_STEP
   /prop_tgt/ANDROID_STL_TYPE
   /prop_tgt/ARCHIVE_OUTPUT_DIRECTORY
   /prop_tgt/ARCHIVE_OUTPUT_DIRECTORY_CONFIG
   /prop_tgt/ARCHIVE_OUTPUT_NAME
   /prop_tgt/ARCHIVE_OUTPUT_NAME_CONFIG
   /prop_tgt/AUTOGEN_BUILD_DIR
   /prop_tgt/AUTOGEN_ORIGIN_DEPENDS
   /prop_tgt/AUTOGEN_PARALLEL
   /prop_tgt/AUTOGEN_TARGET_DEPENDS
   /prop_tgt/AUTOMOC
   /prop_tgt/AUTOMOC_COMPILER_PREDEFINES
   /prop_tgt/AUTOMOC_DEPEND_FILTERS
   /prop_tgt/AUTOMOC_EXECUTABLE
   /prop_tgt/AUTOMOC_MACRO_NAMES
   /prop_tgt/AUTOMOC_MOC_OPTIONS
   /prop_tgt/AUTOMOC_PATH_PREFIX
   /prop_tgt/AUTORCC
   /prop_tgt/AUTORCC_EXECUTABLE
   /prop_tgt/AUTORCC_OPTIONS
   /prop_tgt/AUTOUIC
   /prop_tgt/AUTOUIC_EXECUTABLE
   /prop_tgt/AUTOUIC_OPTIONS
   /prop_tgt/AUTOUIC_SEARCH_PATHS
   /prop_tgt/BINARY_DIR
   /prop_tgt/BUILD_RPATH
   /prop_tgt/BUILD_RPATH_USE_ORIGIN
   /prop_tgt/BUILD_WITH_INSTALL_NAME_DIR
   /prop_tgt/BUILD_WITH_INSTALL_RPATH
   /prop_tgt/BUNDLE
   /prop_tgt/BUNDLE_EXTENSION
   /prop_tgt/C_EXTENSIONS
   /prop_tgt/C_STANDARD
   /prop_tgt/C_STANDARD_REQUIRED
   /prop_tgt/COMMON_LANGUAGE_RUNTIME
   /prop_tgt/COMPATIBLE_INTERFACE_BOOL
   /prop_tgt/COMPATIBLE_INTERFACE_NUMBER_MAX
   /prop_tgt/COMPATIBLE_INTERFACE_NUMBER_MIN
   /prop_tgt/COMPATIBLE_INTERFACE_STRING
   /prop_tgt/COMPILE_DEFINITIONS
   /prop_tgt/COMPILE_FEATURES
   /prop_tgt/COMPILE_FLAGS
   /prop_tgt/COMPILE_OPTIONS
   /prop_tgt/COMPILE_PDB_NAME
   /prop_tgt/COMPILE_PDB_NAME_CONFIG
   /prop_tgt/COMPILE_PDB_OUTPUT_DIRECTORY
   /prop_tgt/COMPILE_PDB_OUTPUT_DIRECTORY_CONFIG
   /prop_tgt/CONFIG_OUTPUT_NAME
   /prop_tgt/CONFIG_POSTFIX
   /prop_tgt/CROSSCOMPILING_EMULATOR
   /prop_tgt/CUDA_ARCHITECTURES
   /prop_tgt/CUDA_EXTENSIONS
   /prop_tgt/CUDA_PTX_COMPILATION
   /prop_tgt/CUDA_RESOLVE_DEVICE_SYMBOLS
   /prop_tgt/CUDA_RUNTIME_LIBRARY
   /prop_tgt/CUDA_SEPARABLE_COMPILATION
   /prop_tgt/CUDA_STANDARD
   /prop_tgt/CUDA_STANDARD_REQUIRED
   /prop_tgt/CXX_EXTENSIONS
   /prop_tgt/CXX_STANDARD
   /prop_tgt/CXX_STANDARD_REQUIRED
   /prop_tgt/DEBUG_POSTFIX
   /prop_tgt/DEFINE_SYMBOL
   /prop_tgt/DEPLOYMENT_ADDITIONAL_FILES
   /prop_tgt/DEPLOYMENT_REMOTE_DIRECTORY
   /prop_tgt/DEPRECATION
   /prop_tgt/DISABLE_PRECOMPILE_HEADERS
   /prop_tgt/DOTNET_TARGET_FRAMEWORK
   /prop_tgt/DOTNET_TARGET_FRAMEWORK_VERSION
   /prop_tgt/EchoString
   /prop_tgt/ENABLE_EXPORTS
   /prop_tgt/EXCLUDE_FROM_ALL
   /prop_tgt/EXCLUDE_FROM_DEFAULT_BUILD
   /prop_tgt/EXCLUDE_FROM_DEFAULT_BUILD_CONFIG
   /prop_tgt/EXPORT_NAME
   /prop_tgt/EXPORT_PROPERTIES
   /prop_tgt/FOLDER
   /prop_tgt/Fortran_FORMAT
   /prop_tgt/Fortran_MODULE_DIRECTORY
   /prop_tgt/Fortran_PREPROCESS
   /prop_tgt/FRAMEWORK
   /prop_tgt/FRAMEWORK_MULTI_CONFIG_POSTFIX_CONFIG
   /prop_tgt/FRAMEWORK_VERSION
   /prop_tgt/GENERATOR_FILE_NAME
   /prop_tgt/GHS_INTEGRITY_APP
   /prop_tgt/GHS_NO_SOURCE_GROUP_FILE
   /prop_tgt/GNUtoMS
   /prop_tgt/HAS_CXX
   /prop_tgt/IMPLICIT_DEPENDS_INCLUDE_TRANSFORM
   /prop_tgt/IMPORTED
   /prop_tgt/IMPORTED_COMMON_LANGUAGE_RUNTIME
   /prop_tgt/IMPORTED_CONFIGURATIONS
   /prop_tgt/IMPORTED_GLOBAL
   /prop_tgt/IMPORTED_IMPLIB
   /prop_tgt/IMPORTED_IMPLIB_CONFIG
   /prop_tgt/IMPORTED_LIBNAME
   /prop_tgt/IMPORTED_LIBNAME_CONFIG
   /prop_tgt/IMPORTED_LINK_DEPENDENT_LIBRARIES
   /prop_tgt/IMPORTED_LINK_DEPENDENT_LIBRARIES_CONFIG
   /prop_tgt/IMPORTED_LINK_INTERFACE_LANGUAGES
   /prop_tgt/IMPORTED_LINK_INTERFACE_LANGUAGES_CONFIG
   /prop_tgt/IMPORTED_LINK_INTERFACE_LIBRARIES
   /prop_tgt/IMPORTED_LINK_INTERFACE_LIBRARIES_CONFIG
   /prop_tgt/IMPORTED_LINK_INTERFACE_MULTIPLICITY
   /prop_tgt/IMPORTED_LINK_INTERFACE_MULTIPLICITY_CONFIG
   /prop_tgt/IMPORTED_LOCATION
   /prop_tgt/IMPORTED_LOCATION_CONFIG
   /prop_tgt/IMPORTED_NO_SONAME
   /prop_tgt/IMPORTED_NO_SONAME_CONFIG
   /prop_tgt/IMPORTED_OBJECTS
   /prop_tgt/IMPORTED_OBJECTS_CONFIG
   /prop_tgt/IMPORTED_SONAME
   /prop_tgt/IMPORTED_SONAME_CONFIG
   /prop_tgt/IMPORT_PREFIX
   /prop_tgt/IMPORT_SUFFIX
   /prop_tgt/INCLUDE_DIRECTORIES
   /prop_tgt/INSTALL_NAME_DIR
   /prop_tgt/INSTALL_REMOVE_ENVIRONMENT_RPATH
   /prop_tgt/INSTALL_RPATH
   /prop_tgt/INSTALL_RPATH_USE_LINK_PATH
   /prop_tgt/INTERFACE_AUTOUIC_OPTIONS
   /prop_tgt/INTERFACE_COMPILE_DEFINITIONS
   /prop_tgt/INTERFACE_COMPILE_FEATURES
   /prop_tgt/INTERFACE_COMPILE_OPTIONS
   /prop_tgt/INTERFACE_INCLUDE_DIRECTORIES
   /prop_tgt/INTERFACE_LINK_DEPENDS
   /prop_tgt/INTERFACE_LINK_DIRECTORIES
   /prop_tgt/INTERFACE_LINK_LIBRARIES
   /prop_tgt/INTERFACE_LINK_OPTIONS
   /prop_tgt/INTERFACE_POSITION_INDEPENDENT_CODE
   /prop_tgt/INTERFACE_PRECOMPILE_HEADERS
   /prop_tgt/INTERFACE_SOURCES
   /prop_tgt/INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
   /prop_tgt/INTERPROCEDURAL_OPTIMIZATION
   /prop_tgt/INTERPROCEDURAL_OPTIMIZATION_CONFIG
   /prop_tgt/IOS_INSTALL_COMBINED
   /prop_tgt/ISPC_HEADER_DIRECTORY
   /prop_tgt/ISPC_HEADER_SUFFIX
   /prop_tgt/ISPC_INSTRUCTION_SETS
   /prop_tgt/JOB_POOL_COMPILE
   /prop_tgt/JOB_POOL_LINK
   /prop_tgt/JOB_POOL_PRECOMPILE_HEADER
   /prop_tgt/LABELS
   /prop_tgt/LANG_CLANG_TIDY
   /prop_tgt/LANG_COMPILER_LAUNCHER
   /prop_tgt/LANG_CPPCHECK
   /prop_tgt/LANG_CPPLINT
   /prop_tgt/LANG_INCLUDE_WHAT_YOU_USE
   /prop_tgt/LANG_VISIBILITY_PRESET
   /prop_tgt/LIBRARY_OUTPUT_DIRECTORY
   /prop_tgt/LIBRARY_OUTPUT_DIRECTORY_CONFIG
   /prop_tgt/LIBRARY_OUTPUT_NAME
   /prop_tgt/LIBRARY_OUTPUT_NAME_CONFIG
   /prop_tgt/LINK_DEPENDS
   /prop_tgt/LINK_DEPENDS_NO_SHARED
   /prop_tgt/LINK_DIRECTORIES
   /prop_tgt/LINK_FLAGS
   /prop_tgt/LINK_FLAGS_CONFIG
   /prop_tgt/LINK_INTERFACE_LIBRARIES
   /prop_tgt/LINK_INTERFACE_LIBRARIES_CONFIG
   /prop_tgt/LINK_INTERFACE_MULTIPLICITY
   /prop_tgt/LINK_INTERFACE_MULTIPLICITY_CONFIG
   /prop_tgt/LINK_LIBRARIES
   /prop_tgt/LINK_OPTIONS
   /prop_tgt/LINK_SEARCH_END_STATIC
   /prop_tgt/LINK_SEARCH_START_STATIC
   /prop_tgt/LINK_WHAT_YOU_USE
   /prop_tgt/LINKER_LANGUAGE
   /prop_tgt/LOCATION
   /prop_tgt/LOCATION_CONFIG
   /prop_tgt/MACHO_COMPATIBILITY_VERSION
   /prop_tgt/MACHO_CURRENT_VERSION
   /prop_tgt/MACOSX_BUNDLE
   /prop_tgt/MACOSX_BUNDLE_INFO_PLIST
   /prop_tgt/MACOSX_FRAMEWORK_INFO_PLIST
   /prop_tgt/MACOSX_RPATH
   /prop_tgt/MANUALLY_ADDED_DEPENDENCIES
   /prop_tgt/MAP_IMPORTED_CONFIG_CONFIG
   /prop_tgt/MSVC_RUNTIME_LIBRARY
   /prop_tgt/NAME
   /prop_tgt/NO_SONAME
   /prop_tgt/NO_SYSTEM_FROM_IMPORTED
   /prop_tgt/OBJC_EXTENSIONS
   /prop_tgt/OBJC_STANDARD
   /prop_tgt/OBJC_STANDARD_REQUIRED
   /prop_tgt/OBJCXX_EXTENSIONS
   /prop_tgt/OBJCXX_STANDARD
   /prop_tgt/OBJCXX_STANDARD_REQUIRED
   /prop_tgt/OPTIMIZE_DEPENDENCIES
   /prop_tgt/OSX_ARCHITECTURES
   /prop_tgt/OSX_ARCHITECTURES_CONFIG
   /prop_tgt/OUTPUT_NAME
   /prop_tgt/OUTPUT_NAME_CONFIG
   /prop_tgt/PCH_WARN_INVALID
   /prop_tgt/PCH_INSTANTIATE_TEMPLATES
   /prop_tgt/PDB_NAME
   /prop_tgt/PDB_NAME_CONFIG
   /prop_tgt/PDB_OUTPUT_DIRECTORY
   /prop_tgt/PDB_OUTPUT_DIRECTORY_CONFIG
   /prop_tgt/POSITION_INDEPENDENT_CODE
   /prop_tgt/PRECOMPILE_HEADERS
   /prop_tgt/PRECOMPILE_HEADERS_REUSE_FROM
   /prop_tgt/PREFIX
   /prop_tgt/PRIVATE_HEADER
   /prop_tgt/PROJECT_LABEL
   /prop_tgt/PUBLIC_HEADER
   /prop_tgt/RESOURCE
   /prop_tgt/RULE_LAUNCH_COMPILE
   /prop_tgt/RULE_LAUNCH_CUSTOM
   /prop_tgt/RULE_LAUNCH_LINK
   /prop_tgt/RUNTIME_OUTPUT_DIRECTORY
   /prop_tgt/RUNTIME_OUTPUT_DIRECTORY_CONFIG
   /prop_tgt/RUNTIME_OUTPUT_NAME
   /prop_tgt/RUNTIME_OUTPUT_NAME_CONFIG
   /prop_tgt/SKIP_BUILD_RPATH
   /prop_tgt/SOURCE_DIR
   /prop_tgt/SOURCES
   /prop_tgt/SOVERSION
   /prop_tgt/STATIC_LIBRARY_FLAGS
   /prop_tgt/STATIC_LIBRARY_FLAGS_CONFIG
   /prop_tgt/STATIC_LIBRARY_OPTIONS
   /prop_tgt/SUFFIX
   /prop_tgt/Swift_DEPENDENCIES_FILE
   /prop_tgt/Swift_LANGUAGE_VERSION
   /prop_tgt/Swift_MODULE_DIRECTORY
   /prop_tgt/Swift_MODULE_NAME
   /prop_tgt/TYPE
   /prop_tgt/UNITY_BUILD
   /prop_tgt/UNITY_BUILD_BATCH_SIZE
   /prop_tgt/UNITY_BUILD_CODE_AFTER_INCLUDE
   /prop_tgt/UNITY_BUILD_CODE_BEFORE_INCLUDE
   /prop_tgt/UNITY_BUILD_MODE
   /prop_tgt/VERSION
   /prop_tgt/VISIBILITY_INLINES_HIDDEN
   /prop_tgt/VS_CONFIGURATION_TYPE
   /prop_tgt/VS_DEBUGGER_COMMAND
   /prop_tgt/VS_DEBUGGER_COMMAND_ARGUMENTS
   /prop_tgt/VS_DEBUGGER_ENVIRONMENT
   /prop_tgt/VS_DEBUGGER_WORKING_DIRECTORY
   /prop_tgt/VS_DESKTOP_EXTENSIONS_VERSION
   /prop_tgt/VS_DOTNET_DOCUMENTATION_FILE
   /prop_tgt/VS_DOTNET_REFERENCE_refname
   /prop_tgt/VS_DOTNET_REFERENCEPROP_refname_TAG_tagname
   /prop_tgt/VS_DOTNET_REFERENCES
   /prop_tgt/VS_DOTNET_REFERENCES_COPY_LOCAL
   /prop_tgt/VS_DOTNET_TARGET_FRAMEWORK_VERSION
   /prop_tgt/VS_DPI_AWARE
   /prop_tgt/VS_GLOBAL_KEYWORD
   /prop_tgt/VS_GLOBAL_PROJECT_TYPES
   /prop_tgt/VS_GLOBAL_ROOTNAMESPACE
   /prop_tgt/VS_GLOBAL_variable
   /prop_tgt/VS_IOT_EXTENSIONS_VERSION
   /prop_tgt/VS_IOT_STARTUP_TASK
   /prop_tgt/VS_JUST_MY_CODE_DEBUGGING
   /prop_tgt/VS_KEYWORD
   /prop_tgt/VS_MOBILE_EXTENSIONS_VERSION
   /prop_tgt/VS_NO_SOLUTION_DEPLOY
   /prop_tgt/VS_PACKAGE_REFERENCES
   /prop_tgt/VS_PLATFORM_TOOLSET
   /prop_tgt/VS_PROJECT_IMPORT
   /prop_tgt/VS_SCC_AUXPATH
   /prop_tgt/VS_SCC_LOCALPATH
   /prop_tgt/VS_SCC_PROJECTNAME
   /prop_tgt/VS_SCC_PROVIDER
   /prop_tgt/VS_SDK_REFERENCES
   /prop_tgt/VS_SOLUTION_DEPLOY
   /prop_tgt/VS_SOURCE_SETTINGS_tool
   /prop_tgt/VS_USER_PROPS
   /prop_tgt/VS_WINDOWS_TARGET_PLATFORM_MIN_VERSION
   /prop_tgt/VS_WINRT_COMPONENT
   /prop_tgt/VS_WINRT_EXTENSIONS
   /prop_tgt/VS_WINRT_REFERENCES
   /prop_tgt/WIN32_EXECUTABLE
   /prop_tgt/WINDOWS_EXPORT_ALL_SYMBOLS
   /prop_tgt/XCODE_ATTRIBUTE_an-attribute
   /prop_tgt/XCODE_EXPLICIT_FILE_TYPE
   /prop_tgt/XCODE_GENERATE_SCHEME
   /prop_tgt/XCODE_LINK_BUILD_PHASE_MODE
   /prop_tgt/XCODE_PRODUCT_TYPE
   /prop_tgt/XCODE_SCHEME_ADDRESS_SANITIZER
   /prop_tgt/XCODE_SCHEME_ADDRESS_SANITIZER_USE_AFTER_RETURN
   /prop_tgt/XCODE_SCHEME_ARGUMENTS
   /prop_tgt/XCODE_SCHEME_DEBUG_AS_ROOT
   /prop_tgt/XCODE_SCHEME_DEBUG_DOCUMENT_VERSIONING
   /prop_tgt/XCODE_SCHEME_DISABLE_MAIN_THREAD_CHECKER
   /prop_tgt/XCODE_SCHEME_DYNAMIC_LIBRARY_LOADS
   /prop_tgt/XCODE_SCHEME_DYNAMIC_LINKER_API_USAGE
   /prop_tgt/XCODE_SCHEME_ENVIRONMENT
   /prop_tgt/XCODE_SCHEME_EXECUTABLE
   /prop_tgt/XCODE_SCHEME_GUARD_MALLOC
   /prop_tgt/XCODE_SCHEME_MAIN_THREAD_CHECKER_STOP
   /prop_tgt/XCODE_SCHEME_MALLOC_GUARD_EDGES
   /prop_tgt/XCODE_SCHEME_MALLOC_SCRIBBLE
   /prop_tgt/XCODE_SCHEME_MALLOC_STACK
   /prop_tgt/XCODE_SCHEME_THREAD_SANITIZER
   /prop_tgt/XCODE_SCHEME_THREAD_SANITIZER_STOP
   /prop_tgt/XCODE_SCHEME_UNDEFINED_BEHAVIOUR_SANITIZER
   /prop_tgt/XCODE_SCHEME_UNDEFINED_BEHAVIOUR_SANITIZER_STOP
   /prop_tgt/XCODE_SCHEME_WORKING_DIRECTORY
   /prop_tgt/XCODE_SCHEME_ZOMBIE_OBJECTS
   /prop_tgt/XCTEST

.. _`Test Properties`:

Properties on Tests
===================

.. toctree::
   :maxdepth: 1

   /prop_test/ATTACHED_FILES
   /prop_test/ATTACHED_FILES_ON_FAIL
   /prop_test/COST
   /prop_test/DEPENDS
   /prop_test/DISABLED
   /prop_test/ENVIRONMENT
   /prop_test/FAIL_REGULAR_EXPRESSION
   /prop_test/FIXTURES_CLEANUP
   /prop_test/FIXTURES_REQUIRED
   /prop_test/FIXTURES_SETUP
   /prop_test/LABELS
   /prop_test/MEASUREMENT
   /prop_test/PASS_REGULAR_EXPRESSION
   /prop_test/PROCESSOR_AFFINITY
   /prop_test/PROCESSORS
   /prop_test/REQUIRED_FILES
   /prop_test/RESOURCE_GROUPS
   /prop_test/RESOURCE_LOCK
   /prop_test/RUN_SERIAL
   /prop_test/SKIP_REGULAR_EXPRESSION
   /prop_test/SKIP_RETURN_CODE
   /prop_test/TIMEOUT
   /prop_test/TIMEOUT_AFTER_MATCH
   /prop_test/WILL_FAIL
   /prop_test/WORKING_DIRECTORY

.. _`Source File Properties`:

Properties on Source Files
==========================

.. toctree::
   :maxdepth: 1

   /prop_sf/ABSTRACT
   /prop_sf/AUTORCC_OPTIONS
   /prop_sf/AUTOUIC_OPTIONS
   /prop_sf/COMPILE_DEFINITIONS
   /prop_sf/COMPILE_FLAGS
   /prop_sf/COMPILE_OPTIONS
   /prop_sf/EXTERNAL_OBJECT
   /prop_sf/Fortran_FORMAT
   /prop_sf/Fortran_PREPROCESS
   /prop_sf/GENERATED
   /prop_sf/HEADER_FILE_ONLY
   /prop_sf/INCLUDE_DIRECTORIES
   /prop_sf/KEEP_EXTENSION
   /prop_sf/LABELS
   /prop_sf/LANGUAGE
   /prop_sf/LOCATION
   /prop_sf/MACOSX_PACKAGE_LOCATION
   /prop_sf/OBJECT_DEPENDS
   /prop_sf/OBJECT_OUTPUTS
   /prop_sf/SKIP_AUTOGEN
   /prop_sf/SKIP_AUTOMOC
   /prop_sf/SKIP_AUTORCC
   /prop_sf/SKIP_AUTOUIC
   /prop_sf/SKIP_PRECOMPILE_HEADERS
   /prop_sf/SKIP_UNITY_BUILD_INCLUSION
   /prop_sf/Swift_DEPENDENCIES_FILE
   /prop_sf/Swift_DIAGNOSTICS_FILE
   /prop_sf/SYMBOLIC
   /prop_sf/UNITY_GROUP
   /prop_sf/VS_COPY_TO_OUT_DIR
   /prop_sf/VS_CSHARP_tagname
   /prop_sf/VS_DEPLOYMENT_CONTENT
   /prop_sf/VS_DEPLOYMENT_LOCATION
   /prop_sf/VS_INCLUDE_IN_VSIX
   /prop_sf/VS_RESOURCE_GENERATOR
   /prop_sf/VS_SETTINGS
   /prop_sf/VS_SHADER_DISABLE_OPTIMIZATIONS
   /prop_sf/VS_SHADER_ENABLE_DEBUG
   /prop_sf/VS_SHADER_ENTRYPOINT
   /prop_sf/VS_SHADER_FLAGS
   /prop_sf/VS_SHADER_MODEL
   /prop_sf/VS_SHADER_OBJECT_FILE_NAME
   /prop_sf/VS_SHADER_OUTPUT_HEADER_FILE
   /prop_sf/VS_SHADER_TYPE
   /prop_sf/VS_SHADER_VARIABLE_NAME
   /prop_sf/VS_TOOL_OVERRIDE.rst
   /prop_sf/VS_XAML_TYPE
   /prop_sf/WRAP_EXCLUDE
   /prop_sf/XCODE_EXPLICIT_FILE_TYPE
   /prop_sf/XCODE_FILE_ATTRIBUTES
   /prop_sf/XCODE_LAST_KNOWN_FILE_TYPE

.. _`Cache Entry Properties`:

Properties on Cache Entries
===========================

.. toctree::
   :maxdepth: 1

   /prop_cache/ADVANCED
   /prop_cache/HELPSTRING
   /prop_cache/MODIFIED
   /prop_cache/STRINGS
   /prop_cache/TYPE
   /prop_cache/VALUE

.. _`Installed File Properties`:

Properties on Installed Files
=============================

.. toctree::
   :maxdepth: 1

   /prop_inst/CPACK_DESKTOP_SHORTCUTS.rst
   /prop_inst/CPACK_NEVER_OVERWRITE.rst
   /prop_inst/CPACK_PERMANENT.rst
   /prop_inst/CPACK_START_MENU_SHORTCUTS.rst
   /prop_inst/CPACK_STARTUP_SHORTCUTS.rst
   /prop_inst/CPACK_WIX_ACL.rst


Deprecated Properties on Directories
====================================

.. toctree::
   :maxdepth: 1

   /prop_dir/ADDITIONAL_MAKE_CLEAN_FILES
   /prop_dir/COMPILE_DEFINITIONS_CONFIG
   /prop_dir/TEST_INCLUDE_FILE


Deprecated Properties on Targets
================================

.. toctree::
   :maxdepth: 1

   /prop_tgt/COMPILE_DEFINITIONS_CONFIG
   /prop_tgt/POST_INSTALL_SCRIPT
   /prop_tgt/PRE_INSTALL_SCRIPT


Deprecated Properties on Source Files
=====================================

.. toctree::
   :maxdepth: 1

   /prop_sf/COMPILE_DEFINITIONS_CONFIG
