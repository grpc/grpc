
set(_compiler_id_pp_test "defined(__VISUALDSPVERSION__) || defined(__ADSPBLACKFIN__) || defined(__ADSPTS__) || defined(__ADSP21000__)")

set(_compiler_id_version_compute "
#if defined(__VISUALDSPVERSION__)
  /* __VISUALDSPVERSION__ = 0xVVRRPP00 */
# define @PREFIX@COMPILER_VERSION_MAJOR @MACRO_HEX@(__VISUALDSPVERSION__>>24)
# define @PREFIX@COMPILER_VERSION_MINOR @MACRO_HEX@(__VISUALDSPVERSION__>>16 & 0xFF)
# define @PREFIX@COMPILER_VERSION_PATCH @MACRO_HEX@(__VISUALDSPVERSION__>>8  & 0xFF)
#endif")
