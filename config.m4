dnl
dnl $Id$
dnl

PHP_ARG_ENABLE(memcache, whether to enable memcache support,
[  --enable-memcache       Enable memcache support])

if test -z "$PHP_ZLIB_DIR"; then
PHP_ARG_WITH(zlib-dir, for the location of libz,
[  --with-zlib-dir[=DIR]   memcache: Set the path to libz install prefix.], no, no)
fi

if test "$PHP_MEMCACHE" != "no"; then

  if test "$PHP_ZLIB_DIR" != "no"; then
    if test -f $PHP_ZLIB_DIR/include/zlib/zlib.h; then
      PHP_ZLIB_DIR=$PHP_ZLIB_DIR
      PHP_ZLIB_INCDIR=$PHP_ZLIB_DIR/include/zlib
    elif test -f $PHP_ZLIB_DIR/include/zlib.h; then
      PHP_ZLIB_DIR=$PHP_ZLIB_DIR
      PHP_ZLIB_INCDIR=$PHP_ZLIB_DIR/include
    fi
  else
    for i in /usr/local /usr; do
      if test -f $i/include/zlib/zlib.h; then
        PHP_ZLIB_DIR=$i
        PHP_ZLIB_INCDIR=$i/include/zlib
      elif test -f $i/include/zlib.h; then
        PHP_ZLIB_DIR=$i
        PHP_ZLIB_INCDIR=$i/include
      fi
    done
  fi

  dnl # zlib
  AC_MSG_CHECKING([for the location of zlib])
  if test "$PHP_ZLIB_DIR" = "no"; then
    AC_MSG_ERROR([memcache support requires ZLIB. Use --with-zlib-dir=<DIR>])
  else
    AC_MSG_RESULT([$PHP_ZLIB_DIR])
    PHP_ADD_LIBRARY_WITH_PATH(z, $PHP_ZLIB_DIR/lib, MEMCACHE_SHARED_LIBADD)
    PHP_ADD_INCLUDE($PHP_ZLIB_INCDIR)
  fi

  AC_DEFINE(HAVE_MEMCACHE,1,[Whether you want memcache support])
  PHP_NEW_EXTENSION(memcache, memcache.c, $ext_shared)
fi


