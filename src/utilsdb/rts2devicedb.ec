/**
 * This add database connectivity to device class
 */
#include "rts2devicedb.h"

EXEC SQL include sqlca;

int
Rts2DeviceDb::willConnect (Rts2Address * in_addr)
{
  if (in_addr->getType () < getDeviceType ()
    || (in_addr->getType () == getDeviceType ()
    && strcmp (in_addr->getName (), getDeviceName ()) < 0))
    return 1;
  return 0;
}


Rts2DeviceDb::Rts2DeviceDb (int in_argc, char **in_argv, int in_device_type,
char *default_name):Rts2Device (in_argc, in_argv, in_device_type, default_name)
{
  connectString = NULL;          // defualt DB
  configFile = NULL;

  addOption ('B', "database", 1, "connect string to PSQL database (default to stars)");
  addOption ('C', "config", 1, "configuration file");
}


Rts2DeviceDb::~Rts2DeviceDb (void)
{
  EXEC SQL DISCONNECT;
  if (connectString)
    delete connectString;
}


int
Rts2DeviceDb::processOption (int in_opt)
{
  switch (in_opt)
  {
    case 'B':
      connectString = new char[strlen (optarg) + 1];
      strcpy (connectString, optarg);
      break;
    case 'C':
      configFile = optarg;
      break;
    default:
      return Rts2Device::processOption (in_opt);
  }
  return 0;
}


int
Rts2DeviceDb::reloadConfig ()
{
  Rts2Config *config;

  // load config..

  config = Rts2Config::instance ();
  return config->loadFile (configFile);
}


int
Rts2DeviceDb::initDB ()
{
  int ret;
  EXEC SQL BEGIN DECLARE SECTION;
    char conn_str[200];
  EXEC SQL END DECLARE SECTION;
  // try to connect to DB

  Rts2Config *config;

  ret = reloadConfig();

  config = Rts2Config::instance ();

  if (ret)
    return ret;

  if (connectString)
  {
    strncpy (conn_str, connectString, 200);
  }
  else
  {
    config->getString ("database", "name", conn_str, 200);
  }
  conn_str[199] = '\0';

  EXEC SQL CONNECT TO :conn_str;
  if (sqlca.sqlcode != 0)
  {
    logStream (MESSAGE_ERROR) << "Rts2DeviceDb::init Cannot connect to DB '" << conn_str << "' : " << sqlca.sqlerrm.sqlerrmc << sendLog;
    return -1;
  }

  return 0;
}


int
Rts2DeviceDb::init ()
{
  int ret;

  ret = Rts2Device::init ();
  if (ret)
    return ret;

  // load config.
  return initDB ();
}


void
Rts2DeviceDb::forkedInstance ()
{
  // dosn't work??
  //  EXEC SQL DISCONNECT;
  Rts2Device::forkedInstance ();
}


void
Rts2DeviceDb::sigHUP (int signal)
{
  reloadConfig();
}
