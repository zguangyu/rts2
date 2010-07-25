#!/usr/bin/python
# (C) 2010, Markus Wildi, markus.wildi@one-arcsec.org
#
#   usage 
#   rts_catalogue.py --help
#   
#   see man 1 rts2_catalogue.py
#   see rts2_autofocus_unittest.py for unit tests
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2, or (at your option)
#   any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software Foundation,
#   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#
#   Or visit http://www.gnu.org/licenses/gpl.html.
#
# required packages:
# wget 'http://argparse.googlecode.com/files/argparse-1.1.zip

__author__ = 'markus.wildi@one-arcsec.org'

import io
import os
import re
import shutil
import string
import sys
import time
from operator import itemgetter, attrgetter


import numpy
import pyfits
import rts2comm 
import rts2af 

class main(rts2af.AFScript):
    """extract the catalgue of an images"""
    def __init__(self, scriptName='main'):
        self.scriptName= scriptName

    def main(self):

        runTimeConfig= rts2af.runTimeConfig = rts2af.Configuration()
        args      = self.arguments()
        logger    = self.configureLogger()
        rts2af.sfo= rts2af.ServiceFileOperations()

        configFileName=''
        if( args.fileName):
            configFileName= args.fileName[0]  
        else:
            configFileName= runTimeConfig.configurationFileName()
            cmd= 'logger no config file specified, taking default' + configFileName
            #print cmd 
            os.system( cmd)

        runTimeConfig.readConfiguration(configFileName)

        print "main " + runTimeConfig.value('SEXPRG')
        print "main " + repr(runTimeConfig.value('TAKE_DATA'))
        
        testFitsList=[]
        testFitsList=rts2af.serviceFileOp.findFitsHDUs()

        paramsSexctractor= rts2af.SExtractorParams()
        paramsSexctractor.readSExtractorParams()

        if( paramsSexctractor==None):
            sys.exit(1)
#        if( rts2af.verbose):
#            for fitsHDUs in testFitsList:
#                print 'FitsHDU to be analyzed: '+ fitsHDUs

        ffs= rts2af.FitsHDUs()
# ToDO, tmp reference catalog, fake
# replace it by a reference catalogue which has the
# the smallest average FWHM for a given filter
# after the validate
#
# loop over fits file names
        hdu= rts2af.FitsHDU('./X/20100625211258-885-RA.fits', True)
        hdu.isFilter('X')
        ffs.append(hdu)

        for fits in testFitsList:
            hdu= rts2af.FitsHDU( fits)
            if(hdu.isFilter('X')):
                ffs.append(hdu)

        ffs.validate()

# loop over hdus
        cats= rts2af.Catalogues()
        for hdu  in ffs.fitsHDUs():
            if( rts2af.verbose):
                print '=======' + hdu.headerElements['FILTER'] + '===' + repr(hdu.isValid) + '= %d' % ffs.fitsHDUs().count(hdu)

            cat= rts2af.Catalogue(hdu)
            cat.extractToCatalogue()
            cat.createCatalogue(paramsSexctractor)
#            cat.printCatalogue()
            cats.append(cat)

        if(cats.validate()):
            print "catalogues is valid"
        else:
            print "catalogues is in valid"

        for cat in sorted(cats.catalogues(), key=lambda cat: cat.fitsHDU.headerElements['FOC_POS']):
            if(rts2af.verbose):
                print "fits file: "+ cat.fitsHDU.fitsFileName + ", %d " % cat.fitsHDU.headerElements['FOC_POS'] 
            cat.average('FWHM_IMAGE')
#            cat.printCatalogue()
            print "============"
            cat.printObject(2)
            cat.removeObject(2)
#            cat.removeObject(2)
#            cat.removeObject(2)
#            cat.removeObject(2)
#            cat.removeObject(2)
            print "============"
            cat.printObject(2)
            cat.writeCatalogue()

        print "THIS IS THE END"
if __name__ == '__main__':
    main(sys.argv[0]).main()



