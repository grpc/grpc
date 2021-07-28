      PROGRAM CMakeFortranCompilerABI
#if 0
! Address Size
#endif
#if defined(_LP64)
        PRINT *, 'INFO:sizeof_dptr[8]'
#elif defined(_M_IA64)
        PRINT *, 'INFO:sizeof_dptr[8]'
#elif defined(_M_X64)
        PRINT *, 'INFO:sizeof_dptr[8]'
#elif defined(_M_AMD64)
        PRINT *, 'INFO:sizeof_dptr[8]'
#elif defined(__x86_64__)
        PRINT *, 'INFO:sizeof_dptr[8]'

#elif defined(_ILP32)
        PRINT *, 'INFO:sizeof_dptr[4]'
#elif defined(_M_IX86)
        PRINT *, 'INFO:sizeof_dptr[4]'
#elif defined(__i386__)
        PRINT *, 'INFO:sizeof_dptr[4]'

#elif defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 8
        PRINT *, 'INFO:sizeof_dptr[8]'
#elif defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 4
        PRINT *, 'INFO:sizeof_dptr[4]'
#elif defined(__SIZEOF_SIZE_T__) && __SIZEOF_SIZE_T__ == 8
        PRINT *, 'INFO:sizeof_dptr[8]'
#elif defined(__SIZEOF_SIZE_T__) && __SIZEOF_SIZE_T__ == 4
        PRINT *, 'INFO:sizeof_dptr[4]'
#endif

#if 0
! Application Binary Interface
#endif
#if defined(__ELF__)
        PRINT *, 'INFO:abi[ELF]'
#endif
        PRINT *, 'ABI Detection'
      END
