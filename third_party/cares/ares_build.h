#ifndef __CARES_BUILD_H
#define __CARES_BUILD_H


/* Copyright (C) 2009 - 2013 by Daniel Stenberg et al
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of M.I.T. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  M.I.T. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 */

/* ================================================================ */
/*               NOTES FOR CONFIGURE CAPABLE SYSTEMS                */
/* ================================================================ */

/*
 * NOTE 1:
 * -------
 *
 * See file ares_build.h.in, run configure, and forget that this file
 * exists it is only used for non-configure systems.
 * But you can keep reading if you want ;-)
 *
 */

/* ================================================================ */
/*                 NOTES FOR NON-CONFIGURE SYSTEMS                  */
/* ================================================================ */

/*
 * NOTE 1:
 * -------
 *
 * Nothing in this file is intended to be modified or adjusted by the
 * c-ares library user nor by the c-ares library builder.
 *
 * If you think that something actually needs to be changed, adjusted
 * or fixed in this file, then, report it on the c-ares development
 * mailing list: http://cool.haxx.se/mailman/listinfo/c-ares/
 *
 * Try to keep one section per platform, compiler and architecture,
 * otherwise, if an existing section is reused for a different one and
 * later on the original is adjusted, probably the piggybacking one can
 * be adversely changed.
 *
 * In order to differentiate between platforms/compilers/architectures
 * use only compiler built in predefined preprocessor symbols.
 *
 * This header file shall only export symbols which are 'cares' or 'CARES'
 * prefixed, otherwise public name space would be polluted.
 *
 * NOTE 2:
 * -------
 *
 * Right now you might be staring at file ares_build.h.dist or ares_build.h,
 * this is due to the following reason: file ares_build.h.dist is renamed
 * to ares_build.h when the c-ares source code distribution archive file is
 * created.
 *
 * File ares_build.h.dist is not included in the distribution archive.
 * File ares_build.h is not present in the git tree.
 *
 * The distributed ares_build.h file is only intended to be used on systems
 * which can not run the also distributed configure script.
 *
 * On systems capable of running the configure script, the configure process
 * will overwrite the distributed ares_build.h file with one that is suitable
 * and specific to the library being configured and built, which is generated
 * from the ares_build.h.in template file.
 *
 * If you check out from git on a non-configure platform, you must run the
 * appropriate buildconf* script to set up ares_build.h and other local files.
 *
 */

/* ================================================================ */
/*  DEFINITION OF THESE SYMBOLS SHALL NOT TAKE PLACE ANYWHERE ELSE  */
/* ================================================================ */

#ifdef CARES_TYPEOF_ARES_SOCKLEN_T
#  error "CARES_TYPEOF_ARES_SOCKLEN_T shall not be defined except in ares_build.h"
   Error Compilation_aborted_CARES_TYPEOF_ARES_SOCKLEN_T_already_defined
#endif

/* ================================================================ */
/*    EXTERNAL INTERFACE SETTINGS FOR NON-CONFIGURE SYSTEMS ONLY    */
/* ================================================================ */

#if defined(__DJGPP__) || defined(__GO32__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__SALFORDC__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__BORLANDC__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__TURBOC__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__WATCOMC__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__POCC__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__LCC__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__SYMBIAN32__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T unsigned int

#elif defined(__MWERKS__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(_WIN32_WCE)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__MINGW32__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__VMS)
#  define CARES_TYPEOF_ARES_SOCKLEN_T unsigned int

#elif defined(__OS400__)
#  if defined(__ILEC400__)
#    define CARES_TYPEOF_ARES_SOCKLEN_T socklen_t
#    define CARES_PULL_SYS_TYPES_H      1
#    define CARES_PULL_SYS_SOCKET_H     1
#  endif

#elif defined(__MVS__)
#  if defined(__IBMC__) || defined(__IBMCPP__)
#    define CARES_TYPEOF_ARES_SOCKLEN_T socklen_t
#    define CARES_PULL_SYS_TYPES_H      1
#    define CARES_PULL_SYS_SOCKET_H     1
#  endif

#elif defined(__370__)
#  if defined(__IBMC__) || defined(__IBMCPP__)
#    define CARES_TYPEOF_ARES_SOCKLEN_T socklen_t
#    define CARES_PULL_SYS_TYPES_H      1
#    define CARES_PULL_SYS_SOCKET_H     1
#  endif

#elif defined(TPF)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

/* ===================================== */
/*    KEEP MSVC THE PENULTIMATE ENTRY    */
/* ===================================== */

#elif defined(_MSC_VER)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

/* ===================================== */
/*    KEEP GENERIC GCC THE LAST ENTRY    */
/* ===================================== */

#elif defined(__GNUC__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T socklen_t
#  define CARES_PULL_SYS_TYPES_H      1
#  define CARES_PULL_SYS_SOCKET_H     1

#else
#  error "Unknown non-configure build target!"
   Error Compilation_aborted_Unknown_non_configure_build_target
#endif

/* CARES_PULL_SYS_TYPES_H is defined above when inclusion of header file  */
/* sys/types.h is required here to properly make type definitions below.  */
#ifdef CARES_PULL_SYS_TYPES_H
#  include <sys/types.h>
#endif

/* CARES_PULL_SYS_SOCKET_H is defined above when inclusion of header file  */
/* sys/socket.h is required here to properly make type definitions below.  */
#ifdef CARES_PULL_SYS_SOCKET_H
#  include <sys/socket.h>
#endif

/* Data type definition of ares_socklen_t. */

#ifdef CARES_TYPEOF_ARES_SOCKLEN_T
  typedef CARES_TYPEOF_ARES_SOCKLEN_T ares_socklen_t;
#endif

/* Data type definition of ares_ssize_t. */
/* gRPC Manuel edit here!
 * Possibly include <_mingw.h> header to define __int64 type under mingw */
#ifdef _WIN32
#  ifdef _WIN64
#    ifdef __MINGW32__
#      include <_mingw.h>
#    endif
#    define CARES_TYPEOF_ARES_SSIZE_T __int64
#  else
#    define CARES_TYPEOF_ARES_SSIZE_T long
#  endif
#else
#  define CARES_TYPEOF_ARES_SSIZE_T ssize_t
#endif

typedef CARES_TYPEOF_ARES_SSIZE_T ares_ssize_t;

/* IMPORTANT: gRPC MANUAL EDIT HERE!
 * Undefine UNICODE, as c-ares does not use the ANSI version of functions
 * explicitly. */
#ifdef UNICODE
#  undef UNICODE
#endif

#ifdef _UNICODE
#  undef _UNICODE
#endif

#endif /* __CARES_BUILD_H */
