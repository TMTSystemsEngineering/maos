#autoscan: scans source file to configure.scan as a template for configure.ac
#aclocal: grenate aclocal.m4 by scanning configure.ac
#autoheader: scan configure.ac to create a header template file config.h.in for config.h
#libtoolize: wraps system libtool to a standard libtool script.
#automake: Creates Makefile.in from Makefile.am
#autoconf: Generates configure from configure.ac

libtoolize --copy || glibtoolize --copy
sed 's/-fno-common//g' m4/libtool.m4 > m4/libtool_new.m4
mv m4/libtool_new.m4 m4/libtool.m4
aclocal --install -I m4
autoconf 
automake --add-missing --copy --foreign
autoheader -f
