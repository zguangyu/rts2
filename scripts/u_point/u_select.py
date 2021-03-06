#!/usr/bin/env python3
# (C) 2016, Markus Wildi, wildi.markus@bluewin.ch
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


'''
Catalog of visible bright and lonely stars 

Yale Bright Star Catalog

http://tdc-www.harvard.edu/catalogs/bsc5.html

'''

__author__ = 'wildi.markus@bluewin.ch'

import sys
import argparse
import logging
import numpy as np

from datetime import datetime
from astropy import units as u
from astropy.time import Time,TimeDelta
from astropy.coordinates import SkyCoord,EarthLocation
from astropy.coordinates import AltAz,CIRS,ITRS
from astropy.coordinates import Longitude,Latitude,Angle
import astropy.coordinates as coord

class Catalog(object):
  def __init__(self, dbg=None,lg=None, obs_lng=None, obs_lat=None, obs_height=None):
    self.dbg=dbg
    self.lg=lg
    #
    self.obs=EarthLocation(lon=float(obs_lng)*u.degree, lat=float(obs_lat)*u.degree, height=float(obs_height)*u.m)
    self.dt_utc = Time(datetime.utcnow(), scale='utc',location=self.obs,out_subfmt='date')

  def observable(self,cat_eq=None, altitude_interval=None,now=False):
    if now:
      cat_aa=cat_eq.transform_to(AltAz(location=self.obs, pressure=0.)) # no refraction here, UTC is in cat_eq
      if altitude_interval[0]<cat_aa.alt.radian<altitude_interval[1]:
        return True
      else:
        return False
    else:
      if self.obs.latitude.radian >= 0.:
        if -(np.pi/2.-self.obs.latitude.radian) < cat_eq.dec.radian:
          return True
      else:
        if -(-np.pi/2.-self.obs.latitude.radian) > cat_eq.dec.radian:
          return True

    return False
    
  def fetch_catalog(self, ptfn=None,bgrightness=None,altitude_interval=None,now=False):

    lns=list()
    with  open(ptfn, 'r') as lfl:
      lns=lfl.readlines()

    self.data=dict()
    self.data['mag_v']=list()
    self.data['sky']=list()
    for i,ln in enumerate(lns):
      try:
        ra_h=int(ln[75:77])
        ra_m=int(ln[77:79])
        ra_s=float(ln[79:83])
        self.lg.debug('{0:4d} HA: {1}:{2}:{3}'.format(i,ra_h,ra_m,ra_s)) 
        dec_n=ln[83:84]
        dec_d=int(ln[84:86])
        dec_m=int(ln[86:88])
        dec_s=float(ln[89:90])
        self.lg.debug('{0:4d} DC: {1}{2}:{3}:{4}'.format(i,dec_n,dec_d,dec_m,dec_s)) 
        mag_v=float(ln[102:107])
      except ValueError:
        self.lg.warn('value error on line: {}'.format(ln[:-1]))
        continue
      except Exception as e:
        self.lg.error('error on line: {}'.format(e))
        sys.exit(1)
        
      if bgrightness[0]<mag_v<bgrightness[1]:
        ra='{} {} {} hours'.format(ra_h,ra_m,ra_s)
        dec='{}{} {} {} '.format(dec_n,dec_d,dec_m,dec_s)
        cat_eq=SkyCoord(ra=ra,dec=dec, unit=(u.hour,u.deg), frame='icrs',obstime=self.dt_utc,location=self.obs)

        if self.observable(cat_eq=cat_eq,altitude_interval=altitude_interval,now=now):
          self.data['sky'].append(cat_eq)
          self.data['mag_v'].append(mag_v)

  def store_observable_catalog(self,ptfn=None):
    with  open(ptfn, 'w') as wfl:
      for i,o in enumerate(self.data['sky']):
        wfl.write('{},{},{}\n'.format(o.ra.radian,o.dec.radian, self.data['mag_v'][i]))

  def plot(self):
    ra = coord.Angle([x.ra.radian for x in self.data['sky']], unit=u.radian)
    ra = ra.wrap_at(180*u.degree)
    dec = coord.Angle([x.dec.radian for x in self.data['sky']],unit=u.radian)

    import matplotlib
    # this varies from distro to distro:
    matplotlib.rcParams["backend"] = "TkAgg"
    import matplotlib.pyplot as plt
    plt.ioff()
    fig = plt.figure(figsize=(8,6))
    ax = fig.add_subplot(111, projection="mollweide")
    # eye candy
    npa=np.asarray([np.exp(x)/10. for x in self.data['mag_v']])
    
    ax.scatter(ra.radian, dec.radian,s=npa)
    ax.set_xticklabels(['14h','16h','18h','20h','22h','0h','2h','4h','6h','8h','10h'])
    ax.grid(True)
    plt.show()

# really ugly!
def arg_floats(value):
  return list(map(float, value.split()))

def arg_float(value):
  if 'm' in value:
    return -float(value[1:])
  else:
    return float(value)

if __name__ == "__main__":

  parser= argparse.ArgumentParser(prog=sys.argv[0], description='Select objects for pointing model observations')
  parser.add_argument('--debug', dest='debug', action='store_true', default=False, help=': %(default)s,add more output')
  parser.add_argument('--level', dest='level', default='INFO', help=': %(default)s, debug level')
  parser.add_argument('--toconsole', dest='toconsole', action='store_true', default=False, help=': %(default)s, log to console')

  parser.add_argument('--obs-longitude', dest='obs_lng', action='store', default=123.2994166666666,type=arg_float, help=': %(default)s [deg], observatory longitude + to the East [deg], negative value: m10. equals to -10.')
  parser.add_argument('--obs-latitude', dest='obs_lat', action='store', default=-75.1,type=arg_float, help=': %(default)s [deg], observatory latitude [deg], negative value: m10. equals to -10.')
  parser.add_argument('--obs-height', dest='obs_height', action='store', default=3237.,type=arg_float, help=': %(default)s [m], observatory height above sea level [m], negative value: m10. equals to -10.')
  parser.add_argument('--yale-catalog', dest='yale_catalog', action='store', default='/usr/share/stardata/yale/catalog.dat', help=': %(default)s, Ubuntu apt install yale')
  parser.add_argument('--plot', dest='plot', action='store_true', default=False, help=': %(default)s, plot results')
  parser.add_argument('--brightness-interval', dest='brightness_interval', default=[0.,7.0], type=arg_floats, help=': %(default)s, visual star brightness [mag], format "p1 p2"')
  parser.add_argument('--altitude-interval',   dest='altitude_interval',   default=[10.,80.],type=arg_floats,help=': %(default)s,  altitude [deg], format "p1 p2"')
  parser.add_argument('--observable-catalog', dest='observable_catalog', action='store', default='observable.cat', help=': %(default)s, store the  observable objects')
  parser.add_argument('--now-observable', dest='now_observable', action='store_true', default=False, help=': %(default)s, select the NOW observable objects, see --altitude-interval')
    
  args=parser.parse_args()
  
  filename='/tmp/{}.log'.format(sys.argv[0].replace('.py','')) # ToDo datetime, name of the script
  logformat= '%(asctime)s:%(name)s:%(levelname)s:%(message)s'
  logging.basicConfig(filename=filename, level=args.level.upper(), format= logformat)
  logger = logging.getLogger()
    
  if args.level in 'DEBUG' or args.level in 'INFO':
    toconsole=True
  else:
    toconsole=args.toconsole
    
  if toconsole:
    # http://www.mglerner.com/blog/?p=8
    soh = logging.StreamHandler(sys.stdout)
    soh.setLevel(args.level)
    logger.addHandler(soh)

  altitude_interval=list()
  for v in args.altitude_interval:
    altitude_interval.append(v/180.*np.pi)

  ct= Catalog(dbg=args.debug,lg=logger,obs_lng=args.obs_lng,obs_lat=args.obs_lat,obs_height=args.obs_height)
  ct.fetch_catalog(ptfn=args.yale_catalog, bgrightness=args.brightness_interval,altitude_interval=altitude_interval,now=args.now_observable)

  ptfn=args.observable_catalog
  if args.now_observable:
    ptfn=ct.dt_utc.iso + '_'+ args.observable_catalog
  
  ct.store_observable_catalog(ptfn=ptfn)
  if args.plot:
    ct.plot()
