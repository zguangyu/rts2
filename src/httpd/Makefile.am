bin_PROGRAMS = rts2-httpd rts2-xmlrpcclient

noinst_HEADERS = xmlstream.h httpd.h r2x.h session.h stateevents.h valueevents.h events.h \
	valueplot.h emailaction.h augerreq.h devicesreq.h planreq.h graphreq.h bbserver.h api.h \
	bbapi.h messageevents.h switchstatereq.h xmlapi.h

LDADD = @MAGIC_LIBS@ @LIB_M@ @LIB_NOVA@ @JSONGLIB_LIBS@
AM_CXXFLAGS = @MAGIC_CFLAGS@ @NOVA_CFLAGS@ @MAGIC_CFLAGS@ @LIBXML_CFLAGS@ @LIBARCHIVE_CFLAGS@ @JSONGLIB_CFLAGS@ -I../../include

if PGSQL

rts2_httpd_SOURCES = httpd.cpp session.cpp events.cpp stateevents.cpp stateeventsdb.cpp valueevents.cpp \
	valueeventsdb.cpp emailaction.cpp valueplot.cpp augerreq.cpp devicesreq.cpp planreq.cpp graphreq.cpp \
	bbserver.cpp api.cpp bbapi.cpp messageevents.cpp switchstatereq.cpp \
	xmlapi.cpp
rts2_httpd_CXXFLAGS = @LIBPG_CFLAGS@ @CFITSIO_CFLAGS@ ${AM_CXXFLAGS}
rts2_httpd_LDADD= -L../../lib/rts2json -lrts2json -L../../lib/rts2scheduler -lrts2scheduler -L../../lib/rts2script -lrts2script -L../../lib/rts2db -lrts2db -L../../lib/pluto -lpluto \
	-L../../lib/rts2fits -lrts2imagedb -L../../lib/rts2 -lrts2 -L../../lib/xmlrpc++ -lrts2xmlrpc @LIBPG_LIBS@ \
	@LIB_ECPG@ @LIB_NOVA@ @CFITSIO_LIBS@ @MAGIC_LIBS@ @LIBXML_LIBS@ @LIB_CRYPT@ @LIBARCHIVE_LIBS@ @LIB_PTHREAD@ $(LDADD)

CLEANFILES = stateeventsdb.cpp valueeventsdb.cpp

.ec.cpp:
	@ECPG@ -o $@ $^

else

rts2_httpd_SOURCES = httpd.cpp session.cpp events.cpp stateevents.cpp valueevents.cpp emailaction.cpp \
	devicesreq.cpp graphreq.cpp bbserver.cpp api.cpp messageevents.cpp switchstatereq.cpp \
	xmlapi.cpp
rts2_httpd_CXXFLAGS = @CFITSIO_CFLAGS@ ${AM_CXXFLAGS}
rts2_httpd_LDADD = -L../../lib/rts2json -lrts2json -L../../lib/rts2script -lrts2script -L../../lib/rts2fits -lrts2image -L../../lib/rts2 -lrts2users -lrts2 -L../../lib/xmlrpc++ -lrts2xmlrpc \
	@LIB_NOVA@ @CFITSIO_LIBS@ @MAGIC_LIBS@ @LIBXML_LIBS@ @LIBARCHIVE_LIBS@ @LIB_CRYPT@ @LIB_PTHREAD@ ${LDADD}

endif

EXTRA_DIST = stateeventsdb.ec valueeventsdb.ec bbapi.cpp

rts2_xmlrpcclient_SOURCES = xmlrpcclient.cpp
rts2_xmlrpcclient_CXXFLAGS = @NOVA_CFLAGS@ ${AM_CXXFLAGS}
rts2_xmlrpcclient_LDADD = -L../../lib/rts2 -lrts2 -L../../lib/xmlrpc++ -lrts2xmlrpc @LIB_NOVA@ $(LDADD)

if JSONSOUP

bin_PROGRAMS += rts2-jsonclient

rts2_jsonclient_SOURCES = jsonclient.cpp
rts2_jsonclient_CXXFLAGS = @NOVA_CFLAGS@ ${AM_CXXFLAGS}
rts2_jsonclient_LDADD = -L../../lib/rts2 -lrts2 -L../../lib/xmlrpc++ -lrts2xmlrpc @LIB_NOVA@ $(LDADD)

else

EXTRA_DIST += jsonclient.cpp

endif
