
/* Size of a pointer-to-data in bytes.  */
#define SIZEOF_DPTR (sizeof(void*))
const char info_sizeof_dptr[] = {
  /* clang-format off */
  'I', 'N', 'F', 'O', ':', 's', 'i', 'z', 'e', 'o', 'f', '_', 'd', 'p', 't',
  'r', '[', ('0' + ((SIZEOF_DPTR / 10) % 10)), ('0' + (SIZEOF_DPTR % 10)), ']',
  '\0'
  /* clang-format on */
};

/* Application Binary Interface.  */

/* Check for (some) ARM ABIs.
 * See e.g. http://wiki.debian.org/ArmEabiPort for some information on this. */
#if defined(__GNU__) && defined(__ELF__) && defined(__ARM_EABI__)
#  define ABI_ID "ELF ARMEABI"
#elif defined(__GNU__) && defined(__ELF__) && defined(__ARMEB__)
#  define ABI_ID "ELF ARM"
#elif defined(__GNU__) && defined(__ELF__) && defined(__ARMEL__)
#  define ABI_ID "ELF ARM"

#elif defined(__linux__) && defined(__ELF__) && defined(__amd64__) &&         \
  defined(__ILP32__)
#  define ABI_ID "ELF X32"

#elif defined(__ELF__)
#  define ABI_ID "ELF"
#endif

#if defined(ABI_ID)
static char const info_abi[] = "INFO:abi[" ABI_ID "]";
#endif
