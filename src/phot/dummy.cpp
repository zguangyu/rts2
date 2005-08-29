/*! 
 * @file Photometer deamon. 
 *
 * @author petr
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define FILTER_STEP  33

#include "phot.h"
#include "../utils/rts2device.h"

#include <signal.h>
#include <time.h>

class Rts2DevPhotDummy:public Rts2DevPhot
{
protected:
  virtual int startIntegrate ();
public:
    Rts2DevPhotDummy (int argc, char **argv);

  virtual long getCount ();

  virtual int ready ()
  {
    return 0;
  };
  virtual int baseInfo ()
  {
    return 0;
  };
  virtual int info ()
  {
    return 0;
  };

  virtual int homeFilter ();
  virtual int moveFilter (int new_filter);
  virtual int enableMove ();
  virtual int disableMove ();
};

Rts2DevPhotDummy::Rts2DevPhotDummy (int argc, char **argv):
Rts2DevPhot (argc, argv)
{
}

long
Rts2DevPhotDummy::getCount ()
{
  sendCount (random (), req_time, 0);
  return (long (req_time * USEC_SEC));
}

int
Rts2DevPhotDummy::homeFilter ()
{
  return 0;
}

int
Rts2DevPhotDummy::startIntegrate ()
{
  return 0;
}

int
Rts2DevPhotDummy::moveFilter (int new_filter)
{
  filter = new_filter;
  return 0;
}

int
Rts2DevPhotDummy::enableMove ()
{
  return 0;
}

int
Rts2DevPhotDummy::disableMove ()
{
  return 0;
}

Rts2DevPhotDummy *device;

void
killSignal (int sig)
{
  if (device)
    delete device;
  exit (0);
}

int
main (int argc, char **argv)
{
  device = new Rts2DevPhotDummy (argc, argv);

  signal (SIGTERM, killSignal);
  signal (SIGINT, killSignal);

  int ret;
  ret = device->init ();
  if (ret)
    {
      syslog (LOG_ERR, "Cannot initialize optec photometer - exiting!\n");
      exit (1);
    }
  device->run ();
  delete device;
}
