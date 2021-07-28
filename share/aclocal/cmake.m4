dnl Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
dnl file Copyright.txt or https://cmake.org/licensing for details.

AC_DEFUN([CMAKE_FIND_BINARY],
[AC_ARG_VAR([CMAKE_BINARY], [path to the cmake binary])dnl

if test "x$ac_cv_env_CMAKE_BINARY_set" != "xset"; then
    AC_PATH_TOOL([CMAKE_BINARY], [cmake])dnl
fi
])dnl

# $1: package name
# $2: language (e.g. C/CXX/Fortran)
# $3: The compiler ID, defaults to GNU.
#     Possible values are: GNU, Intel, Clang, SunPro, HP, XL, VisualAge, PGI,
#     PathScale, Cray, SCO, MSVC
# $4: optional extra arguments to cmake, e.g. "-DCMAKE_SIZEOF_VOID_P=8"
# $5: optional path to cmake binary
AC_DEFUN([CMAKE_FIND_PACKAGE], [
AC_REQUIRE([CMAKE_FIND_BINARY])dnl

AC_ARG_VAR([$1][_][$2][FLAGS], [$2 compiler flags for $1. This overrides the cmake output])dnl
AC_ARG_VAR([$1][_LIBS], [linker flags for $1. This overrides the cmake output])dnl

failed=false
AC_MSG_CHECKING([for $1])
if test -z "${$1[]_$2[]FLAGS}"; then
    $1[]_$2[]FLAGS=`$CMAKE_BINARY --find-package "-DNAME=$1" "-DCOMPILER_ID=m4_default([$3], [GNU])" "-DLANGUAGE=$2" -DMODE=COMPILE $4` || failed=true
fi
if test -z "${$1[]_LIBS}"; then
    $1[]_LIBS=`$CMAKE_BINARY --find-package "-DNAME=$1" "-DCOMPILER_ID=m4_default([$3], [GNU])" "-DLANGUAGE=$2" -DMODE=LINK $4` || failed=true
fi

if $failed; then
    unset $1[]_$2[]FLAGS
    unset $1[]_LIBS

    AC_MSG_RESULT([no])
    $6
else
    AC_MSG_RESULT([yes])
    $5
fi[]dnl
])
