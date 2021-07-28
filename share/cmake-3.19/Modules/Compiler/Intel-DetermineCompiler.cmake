
set(_compiler_id_pp_test "defined(__INTEL_COMPILER) || defined(__ICC)")

set(_compiler_id_version_compute "
  /* __INTEL_COMPILER = VRP */
# define @PREFIX@COMPILER_VERSION_MAJOR @MACRO_DEC@(__INTEL_COMPILER/100)
# define @PREFIX@COMPILER_VERSION_MINOR @MACRO_DEC@(__INTEL_COMPILER/10 % 10)
# if defined(__INTEL_COMPILER_UPDATE)
#  define @PREFIX@COMPILER_VERSION_PATCH @MACRO_DEC@(__INTEL_COMPILER_UPDATE)
# else
#  define @PREFIX@COMPILER_VERSION_PATCH @MACRO_DEC@(__INTEL_COMPILER   % 10)
# endif
# if defined(__INTEL_COMPILER_BUILD_DATE)
  /* __INTEL_COMPILER_BUILD_DATE = YYYYMMDD */
#  define @PREFIX@COMPILER_VERSION_TWEAK @MACRO_DEC@(__INTEL_COMPILER_BUILD_DATE)
# endif
# if defined(_MSC_VER)
   /* _MSC_VER = VVRR */
#  define @PREFIX@SIMULATE_VERSION_MAJOR @MACRO_DEC@(_MSC_VER / 100)
#  define @PREFIX@SIMULATE_VERSION_MINOR @MACRO_DEC@(_MSC_VER % 100)
# endif
# if defined(__GNUC__)
#  define @PREFIX@SIMULATE_VERSION_MAJOR @MACRO_DEC@(__GNUC__)
# elif defined(__GNUG__)
#  define @PREFIX@SIMULATE_VERSION_MAJOR @MACRO_DEC@(__GNUG__)
# endif
# if defined(__GNUC_MINOR__)
#  define @PREFIX@SIMULATE_VERSION_MINOR @MACRO_DEC@(__GNUC_MINOR__)
# endif
# if defined(__GNUC_PATCHLEVEL__)
#  define @PREFIX@SIMULATE_VERSION_PATCH @MACRO_DEC@(__GNUC_PATCHLEVEL__)
# endif")

set(_compiler_id_simulate "
# if defined(_MSC_VER)
#  define @PREFIX@SIMULATE_ID \"MSVC\"
# endif
# if defined(__GNUC__)
#  define @PREFIX@SIMULATE_ID \"GNU\"
# endif")
