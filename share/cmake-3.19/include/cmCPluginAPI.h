/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
/* This header file defines the API that loadable commands can use. In many
   of these commands C++ instances of cmMakefile of cmSourceFile are passed
   in as arguments or returned. In these cases they are passed as a void *
   argument. In the function prototypes mf is used to represent a makefile
   and sf is used to represent a source file. The functions are grouped
   loosely into four groups 1) Utility 2) cmMakefile 3) cmSourceFile 4)
   cmSystemTools. Within each grouping functions are listed alphabetically */
/*=========================================================================*/
#ifndef cmCPluginAPI_h
#define cmCPluginAPI_h

#define CMAKE_VERSION_MAJOR 2
#define CMAKE_VERSION_MINOR 5

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __WATCOMC__
#  define CCONV __cdecl
#else
#  define CCONV
#endif
/*=========================================================================
this is the structure of function entry points that a plugin may call. This
structure must be kept in sync with the static decaled at the bottom of
cmCPLuginAPI.cxx
=========================================================================*/
/* NOLINTNEXTLINE(modernize-use-using) */
typedef struct
{
  /*=========================================================================
  Here we define the set of functions that a plugin may call. The first goup
  of functions are utility functions that are specific to the plugin API
  =========================================================================*/
  /* set/Get the ClientData in the cmLoadedCommandInfo structure, this is how
     information is passed from the InitialPass to FinalPass for commands
     that need a FinalPass and need information from the InitialPass */
  void*(CCONV* GetClientData)(void* info);
  /* return the summed size in characters of all the arguments */
  int(CCONV* GetTotalArgumentSize)(int argc, char** argv);
  /* free all the memory associated with an argc, argv pair */
  void(CCONV* FreeArguments)(int argc, char** argv);
  /* set/Get the ClientData in the cmLoadedCommandInfo structure, this is how
     information is passed from the InitialPass to FinalPass for commands
     that need a FinalPass and need information from the InitialPass */
  void(CCONV* SetClientData)(void* info, void* cd);
  /* when an error occurs, call this function to set the error string */
  void(CCONV* SetError)(void* info, const char* err);

  /*=========================================================================
  The following functions all directly map to methods in the cmMakefile
  class. See cmMakefile.h for descriptions of what each method does. All of
  these methods take the void * makefile pointer as their first argument.
  =========================================================================*/
  void(CCONV* AddCacheDefinition)(void* mf, const char* name,
                                  const char* value, const char* doc,
                                  int cachetype);
  void(CCONV* AddCustomCommand)(void* mf, const char* source,
                                const char* command, int numArgs,
                                const char** args, int numDepends,
                                const char** depends, int numOutputs,
                                const char** outputs, const char* target);
  void(CCONV* AddDefineFlag)(void* mf, const char* definition);
  void(CCONV* AddDefinition)(void* mf, const char* name, const char* value);
  void(CCONV* AddExecutable)(void* mf, const char* exename, int numSrcs,
                             const char** srcs, int win32);
  void(CCONV* AddLibrary)(void* mf, const char* libname, int shared,
                          int numSrcs, const char** srcs);
  void(CCONV* AddLinkDirectoryForTarget)(void* mf, const char* tgt,
                                         const char* d);
  void(CCONV* AddLinkLibraryForTarget)(void* mf, const char* tgt,
                                       const char* libname, int libtype);
  void(CCONV* AddUtilityCommand)(void* mf, const char* utilityName,
                                 const char* command, const char* arguments,
                                 int all, int numDepends, const char** depends,
                                 int numOutputs, const char** outputs);
  int(CCONV* CommandExists)(void* mf, const char* name);
  int(CCONV* ExecuteCommand)(void* mf, const char* name, int numArgs,
                             const char** args);
  void(CCONV* ExpandSourceListArguments)(void* mf, int argc, const char** argv,
                                         int* resArgc, char*** resArgv,
                                         unsigned int startArgumentIndex);
  char*(CCONV* ExpandVariablesInString)(void* mf, const char* source,
                                        int escapeQuotes, int atOnly);
  unsigned int(CCONV* GetCacheMajorVersion)(void* mf);
  unsigned int(CCONV* GetCacheMinorVersion)(void* mf);
  const char*(CCONV* GetCurrentDirectory)(void* mf);
  const char*(CCONV* GetCurrentOutputDirectory)(void* mf);
  const char*(CCONV* GetDefinition)(void* mf, const char* def);
  const char*(CCONV* GetHomeDirectory)(void* mf);
  const char*(CCONV* GetHomeOutputDirectory)(void* mf);
  unsigned int(CCONV* GetMajorVersion)(void* mf);
  unsigned int(CCONV* GetMinorVersion)(void* mf);
  const char*(CCONV* GetProjectName)(void* mf);
  const char*(CCONV* GetStartDirectory)(void* mf);
  const char*(CCONV* GetStartOutputDirectory)(void* mf);
  int(CCONV* IsOn)(void* mf, const char* name);

  /*=========================================================================
  The following functions are designed to operate or manipulate
  cmSourceFiles. Please see cmSourceFile.h for additional information on many
  of these methods. Some of these methods are in cmMakefile.h.
  =========================================================================*/
  void*(CCONV* AddSource)(void* mf, void* sf);
  void*(CCONV* CreateSourceFile)();
  void(CCONV* DestroySourceFile)(void* sf);
  void*(CCONV* GetSource)(void* mf, const char* sourceName);
  void(CCONV* SourceFileAddDepend)(void* sf, const char* depend);
  const char*(CCONV* SourceFileGetProperty)(void* sf, const char* prop);
  int(CCONV* SourceFileGetPropertyAsBool)(void* sf, const char* prop);
  const char*(CCONV* SourceFileGetSourceName)(void* sf);
  const char*(CCONV* SourceFileGetFullPath)(void* sf);
  void(CCONV* SourceFileSetName)(void* sf, const char* name, const char* dir,
                                 int numSourceExtensions,
                                 const char** sourceExtensions,
                                 int numHeaderExtensions,
                                 const char** headerExtensions);
  void(CCONV* SourceFileSetName2)(void* sf, const char* name, const char* dir,
                                  const char* ext, int headerFileOnly);
  void(CCONV* SourceFileSetProperty)(void* sf, const char* prop,
                                     const char* value);

  /*=========================================================================
  The following methods are from cmSystemTools.h see that file for specific
  documentation on each method.
  =========================================================================*/
  char*(CCONV* Capitalized)(const char*);
  void(CCONV* CopyFileIfDifferent)(const char* f1, const char* f2);
  char*(CCONV* GetFilenameWithoutExtension)(const char*);
  char*(CCONV* GetFilenamePath)(const char*);
  void(CCONV* RemoveFile)(const char* f1);
  void(CCONV* Free)(void*);

  /*=========================================================================
    The following are new functions added after 1.6
  =========================================================================*/
  void(CCONV* AddCustomCommandToOutput)(void* mf, const char* output,
                                        const char* command, int numArgs,
                                        const char** args,
                                        const char* main_dependency,
                                        int numDepends, const char** depends);
  void(CCONV* AddCustomCommandToTarget)(void* mf, const char* target,
                                        const char* command, int numArgs,
                                        const char** args, int commandType);

  /* display status information */
  void(CCONV* DisplaySatus)(void* info, const char* message);

  /* new functions added after 2.4 */
  void*(CCONV* CreateNewSourceFile)(void* mf);
  void(CCONV* DefineSourceFileProperty)(void* mf, const char* name,
                                        const char* briefDocs,
                                        const char* longDocs, int chained);

  /* this is the end of the C function stub API structure */
} cmCAPI;

/*=========================================================================
CM_PLUGIN_EXPORT should be used by plugins
=========================================================================*/
#ifdef _WIN32
#  define CM_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define CM_PLUGIN_EXPORT
#endif

/*=========================================================================
define the different types of cache entries, see cmCacheManager.h for more
information
=========================================================================*/
#define CM_CACHE_BOOL 0
#define CM_CACHE_PATH 1
#define CM_CACHE_FILEPATH 2
#define CM_CACHE_STRING 3
#define CM_CACHE_INTERNAL 4
#define CM_CACHE_STATIC 5

/*=========================================================================
define the different types of compiles a library may be
=========================================================================*/
#define CM_LIBRARY_GENERAL 0
#define CM_LIBRARY_DEBUG 1
#define CM_LIBRARY_OPTIMIZED 2

/*=========================================================================
define the different types of custom commands for a target
=========================================================================*/
#define CM_PRE_BUILD 0
#define CM_PRE_LINK 1
#define CM_POST_BUILD 2

/*=========================================================================
Finally we define the key data structures and function prototypes
=========================================================================*/

/* NOLINTNEXTLINE(modernize-use-using) */
typedef const char*(CCONV* CM_DOC_FUNCTION)();

/* NOLINTNEXTLINE(modernize-use-using) */
typedef int(CCONV* CM_INITIAL_PASS_FUNCTION)(void* info, void* mf, int argc,
                                             char* []);

/* NOLINTNEXTLINE(modernize-use-using) */
typedef void(CCONV* CM_FINAL_PASS_FUNCTION)(void* info, void* mf);

/* NOLINTNEXTLINE(modernize-use-using) */
typedef void(CCONV* CM_DESTRUCTOR_FUNCTION)(void* info);

/* NOLINTNEXTLINE(modernize-use-using) */
typedef struct
{
  unsigned long reserved1; /* Reserved for future use.  DO NOT USE.  */
  unsigned long reserved2; /* Reserved for future use.  DO NOT USE.  */
  cmCAPI* CAPI;
  int m_Inherited; /* this ivar is no longer used in CMake 2.2 or later */
  CM_INITIAL_PASS_FUNCTION InitialPass;
  CM_FINAL_PASS_FUNCTION FinalPass;
  CM_DESTRUCTOR_FUNCTION Destructor;
  CM_DOC_FUNCTION GetTerseDocumentation;
  CM_DOC_FUNCTION GetFullDocumentation;
  const char* Name;
  char* Error;
  void* ClientData;
} cmLoadedCommandInfo;

/* NOLINTNEXTLINE(modernize-use-using) */
typedef void(CCONV* CM_INIT_FUNCTION)(cmLoadedCommandInfo*);

#ifdef __cplusplus
}
#endif

#endif
