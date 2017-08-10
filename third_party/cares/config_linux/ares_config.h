/* ares_config.h.  Generated from ares_config.h.in by configure.  */
/* ares_config.h.in.  Generated from configure.ac by autoheader.  */

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* define this if ares is built for a big endian system */
/* #undef ARES_BIG_ENDIAN */

/* when building as static part of libcurl */
/* #undef BUILDING_LIBCURL */

/* Defined for build that exposes internal static functions for testing. */
/* #undef CARES_EXPOSE_STATICS */

/* Defined for build with symbol hiding. */
#define CARES_SYMBOL_HIDING 1

/* Definition to make a library symbol externally visible. */
#define CARES_SYMBOL_SCOPE_EXTERN __attribute__ ((__visibility__ ("default")))

/* Use resolver library to configure cares */
/* #undef CARES_USE_LIBRESOLV */

/* if a /etc/inet dir is being used */
/* #undef ETC_INET */

/* Define to the type of arg 2 for gethostname. */
#define GETHOSTNAME_TYPE_ARG2 size_t

/* Define to the type qualifier of arg 1 for getnameinfo. */
#define GETNAMEINFO_QUAL_ARG1 const

/* Define to the type of arg 1 for getnameinfo. */
#define GETNAMEINFO_TYPE_ARG1 struct sockaddr *

/* Define to the type of arg 2 for getnameinfo. */
#define GETNAMEINFO_TYPE_ARG2 socklen_t

/* Define to the type of args 4 and 6 for getnameinfo. */
#define GETNAMEINFO_TYPE_ARG46 socklen_t

/* Define to the type of arg 7 for getnameinfo. */
#define GETNAMEINFO_TYPE_ARG7 int

/* Specifies the number of arguments to getservbyport_r */
#define GETSERVBYPORT_R_ARGS 6

/* Specifies the size of the buffer to pass to getservbyport_r */
#define GETSERVBYPORT_R_BUFSIZE 4096

/* Define to 1 if you have AF_INET6. */
#define HAVE_AF_INET6 1

/* Define to 1 if you have the <arpa/inet.h> header file. */
#define HAVE_ARPA_INET_H 1

/* Define to 1 if you have the <arpa/nameser_compat.h> header file. */
#define HAVE_ARPA_NAMESER_COMPAT_H 1

/* Define to 1 if you have the <arpa/nameser.h> header file. */
#define HAVE_ARPA_NAMESER_H 1

/* Define to 1 if you have the <assert.h> header file. */
#define HAVE_ASSERT_H 1

/* Define to 1 if you have the `bitncmp' function. */
/* #undef HAVE_BITNCMP */

/* Define to 1 if bool is an available type. */
#define HAVE_BOOL_T 1

/* Define HAVE_CLOCK_GETTIME_MONOTONIC to 1 if you have the clock_gettime
 * function and monotonic timer.
 *
 * Note: setting HAVE_CLOCK_GETTIME_MONOTONIC causes use of the clock_gettime
 * function from glibc, don't set it to support glibc < 2.17 */
#ifndef GPR_BACKWARDS_COMPATIBILITY_MODE
  #define HAVE_CLOCK_GETTIME_MONOTONIC 1
#endif

/* Define to 1 if you have the closesocket function. */
/* #undef HAVE_CLOSESOCKET */

/* Define to 1 if you have the CloseSocket camel case function. */
/* #undef HAVE_CLOSESOCKET_CAMEL */

/* Define to 1 if you have the connect function. */
#define HAVE_CONNECT 1

/* define if the compiler supports basic C++11 syntax */
#define HAVE_CXX11 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the <errno.h> header file. */
#define HAVE_ERRNO_H 1

/* Define to 1 if you have the fcntl function. */
#define HAVE_FCNTL 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have a working fcntl O_NONBLOCK function. */
#define HAVE_FCNTL_O_NONBLOCK 1

/* Define to 1 if you have the freeaddrinfo function. */
#define HAVE_FREEADDRINFO 1

/* Define to 1 if you have a working getaddrinfo function. */
#define HAVE_GETADDRINFO 1

/* Define to 1 if the getaddrinfo function is threadsafe. */
#define HAVE_GETADDRINFO_THREADSAFE 1

/* Define to 1 if you have the getenv function. */
#define HAVE_GETENV 1

/* Define to 1 if you have the gethostbyaddr function. */
#define HAVE_GETHOSTBYADDR 1

/* Define to 1 if you have the gethostbyname function. */
#define HAVE_GETHOSTBYNAME 1

/* Define to 1 if you have the gethostname function. */
#define HAVE_GETHOSTNAME 1

/* Define to 1 if you have the getnameinfo function. */
#define HAVE_GETNAMEINFO 1

/* Define to 1 if you have the getservbyport_r function. */
#define HAVE_GETSERVBYPORT_R 1

/* Define to 1 if you have the `gettimeofday' function. */
#define HAVE_GETTIMEOFDAY 1

/* Define to 1 if you have the `if_indextoname' function. */
#define HAVE_IF_INDEXTONAME 1

/* Define to 1 if you have a IPv6 capable working inet_net_pton function. */
/* #undef HAVE_INET_NET_PTON */

/* Define to 1 if you have a IPv6 capable working inet_ntop function. */
#define HAVE_INET_NTOP 1

/* Define to 1 if you have a IPv6 capable working inet_pton function. */
#define HAVE_INET_PTON 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the ioctl function. */
#define HAVE_IOCTL 1

/* Define to 1 if you have the ioctlsocket function. */
/* #undef HAVE_IOCTLSOCKET */

/* Define to 1 if you have the IoctlSocket camel case function. */
/* #undef HAVE_IOCTLSOCKET_CAMEL */

/* Define to 1 if you have a working IoctlSocket camel case FIONBIO function.
   */
/* #undef HAVE_IOCTLSOCKET_CAMEL_FIONBIO */

/* Define to 1 if you have a working ioctlsocket FIONBIO function. */
/* #undef HAVE_IOCTLSOCKET_FIONBIO */

/* Define to 1 if you have a working ioctl FIONBIO function. */
#define HAVE_IOCTL_FIONBIO 1

/* Define to 1 if you have a working ioctl SIOCGIFADDR function. */
#define HAVE_IOCTL_SIOCGIFADDR 1

/* Define to 1 if you have the `resolve' library (-lresolve). */
/* #undef HAVE_LIBRESOLVE */

/* Define to 1 if you have the <limits.h> header file. */
#define HAVE_LIMITS_H 1

/* if your compiler supports LL */
#define HAVE_LL 1

/* Define to 1 if the compiler supports the 'long long' data type. */
#define HAVE_LONGLONG 1

/* Define to 1 if you have the malloc.h header file. */
#define HAVE_MALLOC_H 1

/* Define to 1 if you have the memory.h header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the MSG_NOSIGNAL flag. */
#define HAVE_MSG_NOSIGNAL 1

/* Define to 1 if you have the <netdb.h> header file. */
#define HAVE_NETDB_H 1

/* Define to 1 if you have the <netinet/in.h> header file. */
#define HAVE_NETINET_IN_H 1

/* Define to 1 if you have the <netinet/tcp.h> header file. */
#define HAVE_NETINET_TCP_H 1

/* Define to 1 if you have the <net/if.h> header file. */
#define HAVE_NET_IF_H 1

/* Define to 1 if you have PF_INET6. */
#define HAVE_PF_INET6 1

/* Define to 1 if you have the recv function. */
#define HAVE_RECV 1

/* Define to 1 if you have the recvfrom function. */
#define HAVE_RECVFROM 1

/* Define to 1 if you have the send function. */
#define HAVE_SEND 1

/* Define to 1 if you have the setsockopt function. */
#define HAVE_SETSOCKOPT 1

/* Define to 1 if you have a working setsockopt SO_NONBLOCK function. */
/* #undef HAVE_SETSOCKOPT_SO_NONBLOCK */

/* Define to 1 if you have the <signal.h> header file. */
#define HAVE_SIGNAL_H 1

/* Define to 1 if sig_atomic_t is an available typedef. */
#define HAVE_SIG_ATOMIC_T 1

/* Define to 1 if sig_atomic_t is already defined as volatile. */
/* #undef HAVE_SIG_ATOMIC_T_VOLATILE */

/* Define to 1 if your struct sockaddr_in6 has sin6_scope_id. */
#define HAVE_SOCKADDR_IN6_SIN6_SCOPE_ID 1

/* Define to 1 if you have the socket function. */
#define HAVE_SOCKET 1

/* Define to 1 if you have the <socket.h> header file. */
/* #undef HAVE_SOCKET_H */

/* Define to 1 if you have the <stdbool.h> header file. */
#define HAVE_STDBOOL_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the strcasecmp function. */
#define HAVE_STRCASECMP 1

/* Define to 1 if you have the strcmpi function. */
/* #undef HAVE_STRCMPI */

/* Define to 1 if you have the strdup function. */
#define HAVE_STRDUP 1

/* Define to 1 if you have the stricmp function. */
/* #undef HAVE_STRICMP */

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the strncasecmp function. */
#define HAVE_STRNCASECMP 1

/* Define to 1 if you have the strncmpi function. */
/* #undef HAVE_STRNCMPI */

/* Define to 1 if you have the strnicmp function. */
/* #undef HAVE_STRNICMP */

/* Define to 1 if you have the <stropts.h> header file. */
#define HAVE_STROPTS_H 1

/* Define to 1 if you have struct addrinfo. */
#define HAVE_STRUCT_ADDRINFO 1

/* Define to 1 if you have struct in6_addr. */
#define HAVE_STRUCT_IN6_ADDR 1

/* Define to 1 if you have struct sockaddr_in6. */
#define HAVE_STRUCT_SOCKADDR_IN6 1

/* if struct sockaddr_storage is defined */
#define HAVE_STRUCT_SOCKADDR_STORAGE 1

/* Define to 1 if you have the timeval struct. */
#define HAVE_STRUCT_TIMEVAL 1

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#define HAVE_SYS_IOCTL_H 1

/* Define to 1 if you have the <sys/param.h> header file. */
#define HAVE_SYS_PARAM_H 1

/* Define to 1 if you have the <sys/select.h> header file. */
#define HAVE_SYS_SELECT_H 1

/* Define to 1 if you have the <sys/socket.h> header file. */
#define HAVE_SYS_SOCKET_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <sys/uio.h> header file. */
#define HAVE_SYS_UIO_H 1

/* Define to 1 if you have the <time.h> header file. */
#define HAVE_TIME_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the windows.h header file. */
/* #undef HAVE_WINDOWS_H */

/* Define to 1 if you have the winsock2.h header file. */
/* #undef HAVE_WINSOCK2_H */

/* Define to 1 if you have the winsock.h header file. */
/* #undef HAVE_WINSOCK_H */

/* Define to 1 if you have the writev function. */
#define HAVE_WRITEV 1

/* Define to 1 if you have the ws2tcpip.h header file. */
/* #undef HAVE_WS2TCPIP_H */

/* Define to the sub-directory in which libtool stores uninstalled libraries.
   */
#define LT_OBJDIR ".libs/"

/* Define to 1 if you need the malloc.h header file even with stdlib.h */
/* #undef NEED_MALLOC_H */

/* Define to 1 if you need the memory.h header file even with stdlib.h */
/* #undef NEED_MEMORY_H */

/* Define to 1 if _REENTRANT preprocessor symbol must be defined. */
/* #undef NEED_REENTRANT */

/* Define to 1 if _THREAD_SAFE preprocessor symbol must be defined. */
/* #undef NEED_THREAD_SAFE */

/* cpu-machine-OS */
#define OS "i386-unknown-linux-gnu"

/* Name of package */
#define PACKAGE "c-ares"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "c-ares mailing list: http://cool.haxx.se/mailman/listinfo/c-ares"

/* Define to the full name of this package. */
#define PACKAGE_NAME "c-ares"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "c-ares -"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "c-ares"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "-"

/* a suitable file/device to read random data from */
#define RANDOM_FILE "/dev/urandom"

/* Define to the type qualifier pointed by arg 5 for recvfrom. */
#define RECVFROM_QUAL_ARG5 

/* Define to the type of arg 1 for recvfrom. */
#define RECVFROM_TYPE_ARG1 int

/* Define to the type pointed by arg 2 for recvfrom. */
#define RECVFROM_TYPE_ARG2 void

/* Define to 1 if the type pointed by arg 2 for recvfrom is void. */
#define RECVFROM_TYPE_ARG2_IS_VOID 1

/* Define to the type of arg 3 for recvfrom. */
#define RECVFROM_TYPE_ARG3 size_t

/* Define to the type of arg 4 for recvfrom. */
#define RECVFROM_TYPE_ARG4 int

/* Define to the type pointed by arg 5 for recvfrom. */
#define RECVFROM_TYPE_ARG5 struct sockaddr

/* Define to 1 if the type pointed by arg 5 for recvfrom is void. */
/* #undef RECVFROM_TYPE_ARG5_IS_VOID */

/* Define to the type pointed by arg 6 for recvfrom. */
#define RECVFROM_TYPE_ARG6 socklen_t

/* Define to 1 if the type pointed by arg 6 for recvfrom is void. */
/* #undef RECVFROM_TYPE_ARG6_IS_VOID */

/* Define to the function return type for recvfrom. */
#define RECVFROM_TYPE_RETV ssize_t

/* Define to the type of arg 1 for recv. */
#define RECV_TYPE_ARG1 int

/* Define to the type of arg 2 for recv. */
#define RECV_TYPE_ARG2 void *

/* Define to the type of arg 3 for recv. */
#define RECV_TYPE_ARG3 size_t

/* Define to the type of arg 4 for recv. */
#define RECV_TYPE_ARG4 int

/* Define to the function return type for recv. */
#define RECV_TYPE_RETV ssize_t

/* Define as the return type of signal handlers (`int' or `void'). */
#define RETSIGTYPE void

/* Define to the type qualifier of arg 2 for send. */
#define SEND_QUAL_ARG2 const

/* Define to the type of arg 1 for send. */
#define SEND_TYPE_ARG1 int

/* Define to the type of arg 2 for send. */
#define SEND_TYPE_ARG2 void *

/* Define to the type of arg 3 for send. */
#define SEND_TYPE_ARG3 size_t

/* Define to the type of arg 4 for send. */
#define SEND_TYPE_ARG4 int

/* Define to the function return type for send. */
#define SEND_TYPE_RETV ssize_t

/* The size of `int', as computed by sizeof. */
#define SIZEOF_INT 4

/* The size of `long', as computed by sizeof. */
#define SIZEOF_LONG 4

/* The size of `short', as computed by sizeof. */
#define SIZEOF_SHORT 2

/* The size of `size_t', as computed by sizeof. */
#define SIZEOF_SIZE_T 4

/* The size of `struct in6_addr', as computed by sizeof. */
#define SIZEOF_STRUCT_IN6_ADDR 16

/* The size of `struct in_addr', as computed by sizeof. */
#define SIZEOF_STRUCT_IN_ADDR 4

/* The size of `time_t', as computed by sizeof. */
#define SIZEOF_TIME_T 4

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define to 1 if you can safely include both <sys/time.h> and <time.h>. */
#define TIME_WITH_SYS_TIME 1

/* Define to disable non-blocking sockets. */
/* #undef USE_BLOCKING_SOCKETS */

/* Version number of package */
#define VERSION "-"

/* Define to avoid automatic inclusion of winsock.h */
/* #undef WIN32_LEAN_AND_MEAN */

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
/* #  undef WORDS_BIGENDIAN */
# endif
#endif

/* Define to 1 if OS is AIX. */
#ifndef _ALL_SOURCE
/* #  undef _ALL_SOURCE */
#endif

/* Enable large inode numbers on Mac OS X 10.5.  */
#ifndef _DARWIN_USE_64_BIT_INODE
# define _DARWIN_USE_64_BIT_INODE 1
#endif

#ifdef GPR_BACKWARDS_COMPATIBILITY_MODE
  /* Redefine the fd_set macros for GLIBC < 2.15 support.
   * This is a backwards compatibility hack. At version 2.15, GLIBC introduces
   * the __fdelt_chk function, and starts using it within its fd_set macros
   * (which c-ares uses). For compatibility with GLIBC < 2.15, we need to redefine
   * the fd_set macros to not use __fdelt_chk. */
  #include <sys/select.h>
  #undef FD_SET
  #undef FD_CLR
  #undef FD_ISSET
  /* 'FD_ZERO' doesn't use __fdelt_chk, no need to redefine. */

  #ifdef __FDS_BITS
    #define GRPC_CARES_FDS_BITS(set) __FDS_BITS(set)
  #else
    #define GRPC_CARES_FDS_BITS(set) ((set)->fds_bits)
  #endif

  #define GRPC_CARES_FD_MASK(d) ((long int)(1UL << (d) % NFDBITS))

  #define FD_SET(d, set) \
      ((void) (GRPC_CARES_FDS_BITS (set)[ (d) / NFDBITS ] |= GRPC_CARES_FD_MASK(d)))
  #define FD_CLR(d, set) \
      ((void) (GRPC_CARES_FDS_BITS (set)[ (d) / NFDBITS ] &= ~GRPC_CARES_FD_MASK(d)))
  #define FD_ISSET(d, set) \
      ((GRPC_CARES_FDS_BITS (set)[ (d) / NFDBITS ] & GRPC_CARES_FD_MASK(d)) != 0)
#endif /* GPR_BACKWARDS_COMPATIBILITY_MODE */

/* Number of bits in a file offset, on hosts where this is settable. */
/* #undef _FILE_OFFSET_BITS */

/* Define for large files, on AIX-style hosts. */
/* #undef _LARGE_FILES */

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* Type to use in place of in_addr_t when system does not provide it. */
/* #undef in_addr_t */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

/* the signed version of size_t */
/* #undef ssize_t */
