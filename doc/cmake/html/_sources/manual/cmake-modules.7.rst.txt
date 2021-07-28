.. cmake-manual-description: CMake Modules Reference

cmake-modules(7)
****************

The modules listed here are part of the CMake distribution.
Projects may provide further modules; their location(s)
can be specified in the :variable:`CMAKE_MODULE_PATH` variable.

Utility Modules
^^^^^^^^^^^^^^^

These modules are loaded using the :command:`include` command.

.. toctree::
   :maxdepth: 1

   /module/AddFileDependencies
   /module/AndroidTestUtilities
   /module/BundleUtilities
   /module/CheckCCompilerFlag
   /module/CheckCompilerFlag
   /module/CheckCSourceCompiles
   /module/CheckCSourceRuns
   /module/CheckCXXCompilerFlag
   /module/CheckCXXSourceCompiles
   /module/CheckCXXSourceRuns
   /module/CheckCXXSymbolExists
   /module/CheckFortranCompilerFlag
   /module/CheckFortranFunctionExists
   /module/CheckFortranSourceCompiles
   /module/CheckFortranSourceRuns
   /module/CheckFunctionExists
   /module/CheckIncludeFileCXX
   /module/CheckIncludeFile
   /module/CheckIncludeFiles
   /module/CheckIPOSupported
   /module/CheckLanguage
   /module/CheckLibraryExists
   /module/CheckLinkerFlag
   /module/CheckOBJCCompilerFlag
   /module/CheckOBJCSourceCompiles
   /module/CheckOBJCSourceRuns
   /module/CheckOBJCXXCompilerFlag
   /module/CheckOBJCXXSourceCompiles
   /module/CheckOBJCXXSourceRuns
   /module/CheckPIESupported
   /module/CheckPrototypeDefinition
   /module/CheckSourceCompiles
   /module/CheckSourceRuns
   /module/CheckStructHasMember
   /module/CheckSymbolExists
   /module/CheckTypeSize
   /module/CheckVariableExists
   /module/CMakeAddFortranSubdirectory
   /module/CMakeBackwardCompatibilityCXX
   /module/CMakeDependentOption
   /module/CMakeFindDependencyMacro
   /module/CMakeFindFrameworks
   /module/CMakeFindPackageMode
   /module/CMakeGraphVizOptions
   /module/CMakePackageConfigHelpers
   /module/CMakePrintHelpers
   /module/CMakePrintSystemInformation
   /module/CMakePushCheckState
   /module/CMakeVerifyManifest
   /module/CPack
   /module/CPackComponent
   /module/CPackIFW
   /module/CPackIFWConfigureFile
   /module/CSharpUtilities
   /module/CTest
   /module/CTestCoverageCollectGCOV
   /module/CTestScriptMode
   /module/CTestUseLaunchers
   /module/Dart
   /module/DeployQt4
   /module/Documentation
   /module/ExternalData
   /module/ExternalProject
   /module/FeatureSummary
   /module/FetchContent
   /module/FindPackageHandleStandardArgs
   /module/FindPackageMessage
   /module/FortranCInterface
   /module/GenerateExportHeader
   /module/GetPrerequisites
   /module/GNUInstallDirs
   /module/GoogleTest
   /module/InstallRequiredSystemLibraries
   /module/ProcessorCount
   /module/SelectLibraryConfigurations
   /module/SquishTestScript
   /module/TestBigEndian
   /module/TestForANSIForScope
   /module/TestForANSIStreamHeaders
   /module/TestForSSTREAM
   /module/TestForSTDNamespace
   /module/UseEcos
   /module/UseJava
   /module/UseJavaClassFilelist
   /module/UseJavaSymlinks
   /module/UseSWIG
   /module/UsewxWidgets
   /module/WriteCompilerDetectionHeader

Find Modules
^^^^^^^^^^^^

These modules search for third-party software.
They are normally called through the :command:`find_package` command.

.. toctree::
   :maxdepth: 1

   /module/FindALSA
   /module/FindArmadillo
   /module/FindASPELL
   /module/FindAVIFile
   /module/FindBacktrace
   /module/FindBISON
   /module/FindBLAS
   /module/FindBoost
   /module/FindBullet
   /module/FindBZip2
   /module/FindCABLE
   /module/FindCoin3D
   /module/FindCUDAToolkit
   /module/FindCups
   /module/FindCURL
   /module/FindCurses
   /module/FindCVS
   /module/FindCxxTest
   /module/FindCygwin
   /module/FindDart
   /module/FindDCMTK
   /module/FindDevIL
   /module/FindDoxygen
   /module/FindEnvModules
   /module/FindEXPAT
   /module/FindFLEX
   /module/FindFLTK
   /module/FindFLTK2
   /module/FindFontconfig
   /module/FindFreetype
   /module/FindGCCXML
   /module/FindGDAL
   /module/FindGettext
   /module/FindGIF
   /module/FindGit
   /module/FindGLEW
   /module/FindGLUT
   /module/FindGnuplot
   /module/FindGnuTLS
   /module/FindGSL
   /module/FindGTest
   /module/FindGTK
   /module/FindGTK2
   /module/FindHDF5
   /module/FindHg
   /module/FindHSPELL
   /module/FindHTMLHelp
   /module/FindIce
   /module/FindIconv
   /module/FindIcotool
   /module/FindICU
   /module/FindImageMagick
   /module/FindIntl
   /module/FindITK
   /module/FindJasper
   /module/FindJava
   /module/FindJNI
   /module/FindJPEG
   /module/FindKDE3
   /module/FindKDE4
   /module/FindLAPACK
   /module/FindLATEX
   /module/FindLibArchive
   /module/FindLibinput
   /module/FindLibLZMA
   /module/FindLibXml2
   /module/FindLibXslt
   /module/FindLTTngUST
   /module/FindLua
   /module/FindLua50
   /module/FindLua51
   /module/FindMatlab
   /module/FindMFC
   /module/FindMotif
   /module/FindMPEG
   /module/FindMPEG2
   /module/FindMPI
   /module/FindODBC
   /module/FindOpenACC
   /module/FindOpenAL
   /module/FindOpenCL
   /module/FindOpenGL
   /module/FindOpenMP
   /module/FindOpenSceneGraph
   /module/FindOpenSSL
   /module/FindOpenThreads
   /module/Findosg
   /module/Findosg_functions
   /module/FindosgAnimation
   /module/FindosgDB
   /module/FindosgFX
   /module/FindosgGA
   /module/FindosgIntrospection
   /module/FindosgManipulator
   /module/FindosgParticle
   /module/FindosgPresentation
   /module/FindosgProducer
   /module/FindosgQt
   /module/FindosgShadow
   /module/FindosgSim
   /module/FindosgTerrain
   /module/FindosgText
   /module/FindosgUtil
   /module/FindosgViewer
   /module/FindosgVolume
   /module/FindosgWidget
   /module/FindPatch
   /module/FindPerl
   /module/FindPerlLibs
   /module/FindPHP4
   /module/FindPhysFS
   /module/FindPike
   /module/FindPkgConfig
   /module/FindPNG
   /module/FindPostgreSQL
   /module/FindProducer
   /module/FindProtobuf
   /module/FindPython
   /module/FindPython2
   /module/FindPython3
   /module/FindQt3
   /module/FindQt4
   /module/FindQuickTime
   /module/FindRTI
   /module/FindRuby
   /module/FindSDL
   /module/FindSDL_image
   /module/FindSDL_mixer
   /module/FindSDL_net
   /module/FindSDL_sound
   /module/FindSDL_ttf
   /module/FindSelfPackers
   /module/FindSquish
   /module/FindSQLite3
   /module/FindSubversion
   /module/FindSWIG
   /module/FindTCL
   /module/FindTclsh
   /module/FindTclStub
   /module/FindThreads
   /module/FindTIFF
   /module/FindUnixCommands
   /module/FindVTK
   /module/FindVulkan
   /module/FindWget
   /module/FindWish
   /module/FindwxWidgets
   /module/FindX11
   /module/FindXalanC
   /module/FindXCTest
   /module/FindXercesC
   /module/FindXMLRPC
   /module/FindZLIB

Deprecated Modules
^^^^^^^^^^^^^^^^^^^

Deprecated Utility Modules
==========================

.. toctree::
   :maxdepth: 1

   /module/CMakeDetermineVSServicePack
   /module/CMakeExpandImportedTargets
   /module/CMakeForceCompiler
   /module/CMakeParseArguments
   /module/MacroAddFileDependencies
   /module/TestCXXAcceptsFlag
   /module/UsePkgConfig
   /module/Use_wxWindows
   /module/WriteBasicConfigVersionFile

Deprecated Find Modules
=======================

.. toctree::
   :maxdepth: 1

   /module/FindCUDA
   /module/FindPythonInterp
   /module/FindPythonLibs
   /module/FindQt
   /module/FindwxWindows

Legacy CPack Modules
====================

These modules used to be mistakenly exposed to the user, and have been moved
out of user visibility. They are for CPack internal use, and should never be
used directly.

.. toctree::
   :maxdepth: 1

   /module/CPackArchive
   /module/CPackBundle
   /module/CPackCygwin
   /module/CPackDeb
   /module/CPackDMG
   /module/CPackFreeBSD
   /module/CPackNSIS
   /module/CPackNuGet
   /module/CPackPackageMaker
   /module/CPackProductBuild
   /module/CPackRPM
   /module/CPackWIX
