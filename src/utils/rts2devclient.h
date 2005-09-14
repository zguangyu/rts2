#ifndef __RTS2_DEVCLIENT__
#define __RTS2_DEVCLIENT__

#include <vector>

#include "rts2object.h"
#include "rts2block.h"
#include "rts2value.h"

class Rts2Command;
class Rts2ClientTCPDataConn;
class Rts2ServerState;

/**************************************
 *
 * Defines default classes for handling device information.
 * 
 * Descendants of rts2client can (by overloading method getConnection)
 * specify devices classes, which e.g. allow X11 printout of device
 * information etc..
 * 
 *************************************/

class Rts2Block;

class Rts2DevClient:public Rts2Object
{
private:
  int failedCount;
protected:
    Rts2Conn * connection;
  enum
  { NOT_PROCESED, PROCESED } processedBaseInfo;
  virtual void getPriority ();
  virtual void lostPriority ();
  virtual void died ();
  enum
  { NOT_WAITING, WAIT_MOVE, WAIT_NOT_POSSIBLE } waiting;	// if we wait for something..
  virtual void blockWait ();
  virtual void unblockWait ();
  virtual void unsetWait ();
  virtual void clearWait ();
  virtual int isWaitMove ();
  virtual void setWaitMove ();

  void incFailedCount ()
  {
    failedCount++;
  }
  int getFailedCount ()
  {
    return failedCount;
  }
  void clearFailedCount ()
  {
    failedCount = 0;
  }
public:
  Rts2DevClient (Rts2Conn * in_connection);
  virtual ~ Rts2DevClient (void);
  virtual void postEvent (Rts2Event * event);
  void addValue (Rts2Value * value);
  Rts2Value *getValue (char *value_name);
  char *getValueChar (char *value_name);
  double getValueDouble (char *value_name);
  int getValueInteger (char *value_name);

  virtual int commandValue (const char *name);
  virtual int command ();

  std::vector < Rts2Value * >values;

  virtual void dataReceived (Rts2ClientTCPDataConn * dataConn)
  {
  }

  virtual void stateChanged (Rts2ServerState * state);

  const char *getName ();

  Rts2Block *getMaster ();

  int queCommand (Rts2Command * cmd);

  int commandReturn (Rts2Command * cmd, int status)
  {
    if (status)
      incFailedCount ();
    else
      clearFailedCount ();
    return 0;
  }
};

/**************************************
 *
 * Classes for devices connections.
 *
 * Please note that we cannot use descendants of Rts2ConnClient,
 * since we can connect to device as server.
 * 
 *************************************/

class Rts2DevClientCamera:public Rts2DevClient
{
protected:
  virtual void exposureStarted ();
  virtual void exposureEnd ();
  virtual void readoutEnd ();
public:
    Rts2DevClientCamera (Rts2Conn * in_connection);
  // exposureFailed will get called even when we faild during readout
  virtual void exposureFailed (int status);
  virtual void stateChanged (Rts2ServerState * state);

  virtual void filterOK ()
  {
  }
  virtual void filterFailed ()
  {
  }
};

class Rts2DevClientTelescope:public Rts2DevClient
{
protected:
  virtual void moveStart ();
  virtual void moveEnd ();
  virtual void searchStart ();
  virtual void searchEnd ();
public:
    Rts2DevClientTelescope (Rts2Conn * in_connection);
    virtual ~ Rts2DevClientTelescope (void);
  /*! gets calledn when move finished without success */
  virtual void moveFailed (int status)
  {
  }
  virtual void searchFailed (int status)
  {
  }
  virtual void stateChanged (Rts2ServerState * state);
};

class Rts2DevClientDome:public Rts2DevClient
{
public:
  Rts2DevClientDome (Rts2Conn * in_connection);
};

class Rts2DevClientCopula:public Rts2DevClientDome
{
protected:
  virtual void syncStarted ();
  virtual void syncEnded ();
public:
    Rts2DevClientCopula (Rts2Conn * in_connection);
  virtual void syncFailed (int status);
  virtual void stateChanged (Rts2ServerState * state);
};

class Rts2DevClientMirror:public Rts2DevClient
{
protected:
  virtual void mirrorA ();
  virtual void mirrorB ();
public:
    Rts2DevClientMirror (Rts2Conn * in_connection);
    virtual ~ Rts2DevClientMirror (void);
  virtual void moveFailed (int status);
  virtual void stateChanged (Rts2ServerState * state);
};

class Rts2DevClientFocus:public Rts2DevClient
{
protected:
  virtual void focusingStart ();
  virtual void focusingEnd ();
public:
    Rts2DevClientFocus (Rts2Conn * in_connection);
    virtual ~ Rts2DevClientFocus (void);
  virtual void focusingFailed (int status);
  virtual void stateChanged (Rts2ServerState * state);
};

class Rts2DevClientPhot:public Rts2DevClient
{
protected:
  virtual void integrationStart ();
  virtual void integrationEnd ();
  virtual void addCount (int count, float exp, int is_ov);
  int lastCount;
  float lastExp;
public:
    Rts2DevClientPhot (Rts2Conn * in_connection);
    virtual ~ Rts2DevClientPhot (void);
  virtual int commandValue (const char *name);
  virtual void integrationFailed (int status);
  virtual void stateChanged (Rts2ServerState * state);

  virtual void filterOK ()
  {
  }
  virtual void filterFailed ()
  {
  }
};

class Rts2DevClientExecutor:public Rts2DevClient
{
public:
  Rts2DevClientExecutor (Rts2Conn * in_connection);
};

class Rts2DevClientSelector:public Rts2DevClient
{
public:
  Rts2DevClientSelector (Rts2Conn * in_connection);
};

class Rts2DevClientImgproc:public Rts2DevClient
{
public:
  Rts2DevClientImgproc (Rts2Conn * in_connection);
  virtual int command ();
};

class Rts2DevClientGrb:public Rts2DevClient
{
public:
  Rts2DevClientGrb (Rts2Conn * in_connection);
};

#endif /* !__RTS2_DEVCLIENT__ */
