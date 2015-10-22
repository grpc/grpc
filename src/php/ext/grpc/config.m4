PHP_ARG_ENABLE(grpc, whether to enable grpc support,
[  --enable-grpc           Enable grpc support])

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
    *darwin*) ;;
    *)
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
    server_credentials.c php_grpc.c, $ext_shared, , -Wall -Werror -std=c11)
fi
