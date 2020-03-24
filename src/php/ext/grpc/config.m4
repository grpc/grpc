PHP_ARG_ENABLE(grpc, whether to enable grpc support,
[  --enable-grpc           Enable grpc support])

PHP_ARG_ENABLE(coverage, whether to include code coverage symbols,
[  --enable-coverage       Enable coverage support], no, no)

PHP_ARG_ENABLE(tests, whether to compile helper methods for tests,
[  --enable-tests          Enable tests methods], no, no)

dnl Check whether to enable tests
if test "$PHP_TESTS" != "no"; then
  CPPFLAGS="$CPPFLAGS -DGRPC_PHP_DEBUG"
fi

if test "$PHP_GRPC" != "no"; then
  dnl Write more examples of tests here...

  dnl # --with-grpc -> check with-path
  SEARCH_PATH="/usr/local /usr"     # you might want to change this
  SEARCH_FOR="include/grpc/grpc.h"  # you most likely want to change this
  if test -r $PHP_GRPC/$SEARCH_FOR; then # path given as parameter
    GRPC_DIR=$PHP_GRPC
  else # search default path list
    AC_MSG_CHECKING([for grpc files in default path])
    for i in $SEARCH_PATH ; do
      if test -r $i/$SEARCH_FOR; then
        GRPC_DIR=$i
        AC_MSG_RESULT(found in $i)
      fi
    done
  fi
  if test -z "$GRPC_DIR"; then
    AC_MSG_RESULT([not found])
    AC_MSG_ERROR([Please reinstall the grpc distribution])
  fi

  dnl # --with-grpc -> add include path
  PHP_ADD_INCLUDE($GRPC_DIR/include)

  LIBS="-lpthread $LIBS"

  dnl  PHP_ADD_LIBRARY(pthread,,GRPC_SHARED_LIBADD)
  GRPC_SHARED_LIBADD="-lpthread $GRPC_SHARED_LIBADD"
  PHP_ADD_LIBRARY(pthread)
  PHP_ADD_LIBRARY(dl,,GRPC_SHARED_LIBADD)
  PHP_ADD_LIBRARY(dl)

  case $host in
    *darwin*)
      PHP_ADD_LIBRARY(c++,1,GRPC_SHARED_LIBADD)
      ;;
    *)
      PHP_ADD_LIBRARY(stdc++,1,GRPC_SHARED_LIBADD)
      PHP_ADD_LIBRARY(rt,,GRPC_SHARED_LIBADD)
      PHP_ADD_LIBRARY(rt)
      ;;
  esac

  GRPC_LIBDIR=$GRPC_DIR/${GRPC_LIB_SUBDIR-lib}

  PHP_ADD_LIBPATH($GRPC_LIBDIR)

  PHP_CHECK_LIBRARY(gpr,gpr_now,
  [
    PHP_ADD_LIBRARY(gpr,,GRPC_SHARED_LIBADD)
    PHP_ADD_LIBRARY(gpr)
    AC_DEFINE(HAVE_GPRLIB,1,[ ])
  ],[
    AC_MSG_ERROR([wrong gpr lib version or lib not found])
  ],[
    -L$GRPC_LIBDIR
  ])

  PHP_CHECK_LIBRARY(grpc,grpc_channel_destroy,
  [
    PHP_ADD_LIBRARY(grpc,,GRPC_SHARED_LIBADD)
    dnl PHP_ADD_LIBRARY_WITH_PATH(grpc, $GRPC_DIR/lib, GRPC_SHARED_LIBADD)
    AC_DEFINE(HAVE_GRPCLIB,1,[ ])
  ],[
    AC_MSG_ERROR([wrong grpc lib version or lib not found])
  ],[
    -L$GRPC_LIBDIR
  ])

  PHP_SUBST(GRPC_SHARED_LIBADD)

  PHP_NEW_EXTENSION(grpc, byte_buffer.c call.c call_credentials.c channel.c \
    channel_credentials.c completion_queue.c timeval.c server.c \
    server_credentials.c php_grpc.c, $ext_shared, , -Wall -Werror -std=c11 -DGRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK=1)
fi

if test "$PHP_COVERAGE" = "yes"; then

  if test "$GCC" != "yes"; then
    AC_MSG_ERROR([GCC is required for --enable-coverage])
  fi

  dnl Check if ccache is being used
  case `$php_shtool path $CC` in
    *ccache*[)] gcc_ccache=yes;;
    *[)] gcc_ccache=no;;
  esac

  if test "$gcc_ccache" = "yes" && (test -z "$CCACHE_DISABLE" || test "$CCACHE_DISABLE" != "1"); then
    AC_MSG_ERROR([ccache must be disabled when --enable-coverage option is used. You can disable ccache by setting environment variable CCACHE_DISABLE=1.])
  fi

  lcov_version_list="1.5 1.6 1.7 1.9 1.10 1.11 1.12 1.13"

  AC_CHECK_PROG(LCOV, lcov, lcov)
  AC_CHECK_PROG(GENHTML, genhtml, genhtml)
  PHP_SUBST(LCOV)
  PHP_SUBST(GENHTML)

  if test "$LCOV"; then
    AC_CACHE_CHECK([for lcov version], php_cv_lcov_version, [
      php_cv_lcov_version=invalid
      lcov_version=`$LCOV -v 2>/dev/null | $SED -e 's/^.* //'` #'
      for lcov_check_version in $lcov_version_list; do
        if test "$lcov_version" = "$lcov_check_version"; then
          php_cv_lcov_version="$lcov_check_version (ok)"
        fi
      done
    ])
  else
    lcov_msg="To enable code coverage reporting you must have one of the following LCOV versions installed: $lcov_version_list"
    AC_MSG_ERROR([$lcov_msg])
  fi

  case $php_cv_lcov_version in
    ""|invalid[)]
      lcov_msg="You must have one of the following versions of LCOV: $lcov_version_list (found: $lcov_version)."
      AC_MSG_ERROR([$lcov_msg])
      LCOV="exit 0;"
      ;;
  esac

  if test -z "$GENHTML"; then
    AC_MSG_ERROR([Could not find genhtml from the LCOV package])
  fi

  PHP_ADD_MAKEFILE_FRAGMENT

  dnl Remove all optimization flags from CFLAGS
  changequote({,})
  CFLAGS=`echo "$CFLAGS" | $SED -e 's/-O[0-9s]*//g'`
  CXXFLAGS=`echo "$CXXFLAGS" | $SED -e 's/-O[0-9s]*//g'`
  changequote([,])

  dnl Add the special gcc flags
  CFLAGS="$CFLAGS -O0 -ggdb -fprofile-arcs -ftest-coverage"
  CXXFLAGS="$CXXFLAGS -ggdb -O0 -fprofile-arcs -ftest-coverage"
fi
