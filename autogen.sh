#!/bin/sh

AC_VERSION=1.7

aclocal-$AC_VERSION && libtoolize && automake-$AC_VERSION --add-missing && autoconf
