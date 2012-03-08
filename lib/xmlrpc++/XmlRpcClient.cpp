
#include "XmlRpcClient.h"

#include "XmlRpcSocket.h"
#include "XmlRpc.h"

#include "base64.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sstream>

using namespace XmlRpc;

// Static data
const char XmlRpcClient::REQUEST_BEGIN[] =
"<?xml version=\"1.0\"?>\r\n"
"<methodCall><methodName>";
const char XmlRpcClient::REQUEST_END_METHODNAME[] = "</methodName>\r\n";
const char XmlRpcClient::PARAMS_TAG[] = "<params>";
const char XmlRpcClient::PARAMS_ETAG[] = "</params>";
const char XmlRpcClient::PARAM_TAG[] = "<param>";
const char XmlRpcClient::PARAM_ETAG[] =  "</param>";
const char XmlRpcClient::REQUEST_END[] = "</methodCall>\r\n";
const char XmlRpcClient::METHODRESPONSE_TAG[] = "<methodResponse>";
const char XmlRpcClient::FAULT_TAG[] = "<fault>";

XmlRpcClient::XmlRpcClient(char *path, const char **uri)
{
	char *host;
	int port = 80;
	char *authorization = NULL;
	*uri = NULL;

	// try to parse path..

	if (strstr(path,"http://") == path)
		host = path + 7;
	else
		host = path;
	
	// find authorization - look for @
	char *str = strchr(host,'@');
	if (str)
	{
		authorization = host;
		*str = '\0';
		host = str + 1;
	}

	// separate host and uri
	str = strchr (host, '/');
	if (str)
	{
		*str = '\0';
		*uri = str + 1;
	}

	setupHost(host,port,authorization,*uri);
}

XmlRpcClient::XmlRpcClient(const char *host, int port, const char *authorization, const char *uri)
{
	setupHost(host,port,authorization,uri);
}


XmlRpcClient::~XmlRpcClient()
{
}


// Close the owned fd
void XmlRpcClient::close()
{
	XmlRpcUtil::log(4, "XmlRpcClient::close: fd %d.", getfd());
	_connectionState = NO_CONNECTION;
	_disp.exit();
	_disp.removeSource(this);
	XmlRpcSource::close();
}


// Clear the referenced flag even if exceptions or errors occur.
struct ClearFlagOnExit
{
	ClearFlagOnExit(ExecutingType& flag) : _flag(flag) {}
	~ClearFlagOnExit() { _flag = NOEXEC; }
	ExecutingType& _flag;
};

// Execute the named procedure on the remote server.
// Params should be an array of the arguments for the method.
// Returns true if the request was sent and a result received (although the result
// might be a fault).
bool XmlRpcClient::execute(const char* method, XmlRpcValue const& params, XmlRpcValue& result)
{
	XmlRpcUtil::log(1, "XmlRpcClient::execute: method %s (_connectionState %d).", method, _connectionState);

	// This is not a thread-safe operation, if you want to do multithreading, use separate
	// clients for each thread. If you want to protect yourself from multiple threads
	// accessing the same client, replace this code with a real mutex.
	if (_executing != NOEXEC)
		return false;

	_executing = XML_RPC;
	ClearFlagOnExit cf(_executing);

	_sendAttempts = 0;
	_isFault = false;

	if ( ! setupConnection())
		return false;

	if ( ! generateRequest(method, params))
		return false;

	result.clear();
	double msTime = -1.0;		 // Process until exit is called
	_disp.work(msTime);

	if (_connectionState != IDLE || ! parseResponse(result))
		return false;

	XmlRpcUtil::log(1, "XmlRpcClient::execute: method %s completed.", method);

	if (_response_buf)
		free(_response_buf);
	_response_buf = NULL;
	_response_length = 0;

	return true;
}

bool XmlRpcClient::executeGet(const char* path, char* &reply, int &reply_length)
{
	XmlRpcUtil::log(1, "XmlRpcClient::execute: path %s (_connectionState %d):", path, _connectionState);

	if (_executing != NOEXEC)
		return false;
	
	_executing = HTTP_GET;
	ClearFlagOnExit cf(_executing);

	_sendAttempts = 0;
	_isFault = false;

	if ( ! setupConnection())
		return false;

	if ( ! generateGetRequest(path))
		return false;

	double msTime = -1.0;
	_disp.work(msTime);

	if (_connectionState != IDLE)
		return false;

	reply_length = _response_length;
	reply = new char[reply_length + 1];
	memcpy (reply, _response_buf, reply_length);
	reply[reply_length] = '\0';
	XmlRpcUtil::log(1, "XmlRpcClient::executeGet: path %s retrieved. Reply size: %i", path, reply_length);
	return true;
}

// XmlRpcSource interface implementation
// Handle server responses. Called by the event dispatcher during execute.
unsigned XmlRpcClient::handleEvent(unsigned eventType)
{
	if (eventType == XmlRpcDispatch::Exception)
	{
		if (_connectionState == WRITE_REQUEST && _bytesWritten == 0)
			XmlRpcUtil::error("Error in XmlRpcClient::handleEvent: could not connect to server (%s).",
				XmlRpcSocket::getErrorMsg().c_str());
		else
			XmlRpcUtil::error("Error in XmlRpcClient::handleEvent (state %d): %s.",
				_connectionState, XmlRpcSocket::getErrorMsg().c_str());
		return 0;
	}

	if (_connectionState == WRITE_REQUEST)
		if ( ! writeRequest()) return 0;

	if (_connectionState == READ_HEADER)
		if ( ! readHeader()) return 0;

	if (_connectionState == READ_RESPONSE)
		if ( ! readResponse()) return 0;

	// This should probably always ask for Exception events too
	return (_connectionState == WRITE_REQUEST)
		? XmlRpcDispatch::WritableEvent : XmlRpcDispatch::ReadableEvent;
}

// Create the socket connection to the server if necessary
bool XmlRpcClient::setupConnection()
{
	// If an error occurred last time through, or if the server closed the connection, close our end
	if ((_connectionState != NO_CONNECTION && _connectionState != IDLE) || _eof)
		close();

	_eof = false;
	if (_connectionState == NO_CONNECTION)
		if (! doConnect())
			return false;

	// Prepare to write the request
	_connectionState = WRITE_REQUEST;
	_bytesWritten = 0;

	// Notify the dispatcher to listen on this source (calls handleEvent when the socket is writable)
	_disp.removeSource(this);	 // Make sure nothing is left over
	_disp.addSource(this, XmlRpcDispatch::WritableEvent | XmlRpcDispatch::Exception);

	return true;
}

// Connect to the xmlrpc server
bool XmlRpcClient::doConnect()
{
	int fd = XmlRpcSocket::socket();
	if (fd < 0)
	{
		XmlRpcUtil::error("Error in XmlRpcClient::doConnect: Could not create socket (%s).", XmlRpcSocket::getErrorMsg().c_str());
		return false;
	}

	XmlRpcUtil::log(3, "XmlRpcClient::doConnect: fd %d.", fd);
	this->setfd(fd);

	// Don't block on connect/reads/writes
	if ( ! XmlRpcSocket::setNonBlocking(fd))
	{
		this->close();
		XmlRpcUtil::error("Error in XmlRpcClient::doConnect: Could not set socket to non-blocking IO mode (%s).", XmlRpcSocket::getErrorMsg().c_str());
		return false;
	}

	if (_proxy_port > 0)
	{
		if (!XmlRpcSocket::connect(fd, _proxy_host, _proxy_port))
		{
			this->close();
			XmlRpcUtil::error("Error in XmlRpcClient::doConnect: Could not connect to proxy server (%s).", XmlRpcSocket::getErrorMsg().c_str());
			return false;
		}
	}
	else
	{
		if (!XmlRpcSocket::connect(fd, _host, _port))
		{
			this->close();
			XmlRpcUtil::error("Error in XmlRpcClient::doConnect: Could not connect to server (%s).", XmlRpcSocket::getErrorMsg().c_str());
			return false;
		}
	}

	return true;
}

// Encode the request to call the specified method with the specified parameters into xml
bool XmlRpcClient::generateRequest(const char* methodName, XmlRpcValue const& params)
{
	std::string body = REQUEST_BEGIN;
	body += methodName;
	body += REQUEST_END_METHODNAME;

	// If params is an array, each element is a separate parameter
	if (params.valid())
	{
		body += PARAMS_TAG;
		if (params.getType() == XmlRpcValue::TypeArray)
		{
			for (int i=0; i<params.size(); ++i)
			{
				body += PARAM_TAG;
				body += params[i].toXml();
				body += PARAM_ETAG;
			}
		}
		else
		{
			body += PARAM_TAG;
			body += params.toXml();
			body += PARAM_ETAG;
		}

		body += PARAMS_ETAG;
	}
	body += REQUEST_END;

	std::string header = generateHeader(body);
	XmlRpcUtil::log(4, "XmlRpcClient::generateRequest: header is %d bytes, content-length is %d.",
		header.length(), body.length());

	_request = header + body;
	return true;
}

bool XmlRpcClient::generateGetRequest(const char* path)
{
	if (path)
		_request = generateGetHeader(path);
	else
		_request = generateGetHeader(std::string(""));
	XmlRpcUtil::log(4, "XmlRpcClient::generateGetRequest: header is %d bytes.", _request.length ());
	return true;
}

// Prepend http headers
std::string XmlRpcClient::generateHeader(std::string const& body)
{
	std::string header = "POST " + _uri + " HTTP/1.1\r\nUser-Agent: ";
	header += XMLRPC_VERSION;
	header += "\r\nHost: ";
	header += _host;

	char buff[40];
	if (_port != 80)
		sprintf(buff,":%d\r\n", _port);
	else
		sprintf(buff,"\r\n");
	header += buff;

	if (_authorization.length() > 0)
	{
		std::string auth;

		int iostatus = 0;
		base64<char> encoder;
		std::back_insert_iterator<std::string> ins = std::back_inserter(auth);
		encoder.put(_authorization.begin(), _authorization.end(), ins, iostatus, base64<>::crlf());

		header += "Authorization: Basic " + auth + "\r\n";
	}
	header += "Content-Type: text/xml\r\nContent-Length: ";

	sprintf(buff,"%i\r\n\r\n", (int) body.size());

	return header + buff;
}

// Prepare GET request header
std::string XmlRpcClient::generateGetHeader(std::string const& path, std::string const& body)
{
	std::string header = "GET ";

	char buff[40];
	if (_port != 80)
		sprintf(buff,":%d", _port);
	else
		buff[0] = '\0';

	if (_proxy_port > 0)
	{
		header += "http://";
	  	header += _host;
		header += buff;
	}
	if (path[0] != '/')
		header += '/';
	header += path + " HTTP/1.1\r\nUser-Agent: ";
	header += XMLRPC_VERSION;
	header += "\r\nHost: ";
	header += _host;
	header += buff;
	header += "\r\n";

	if (_authorization.length() > 0)
	{
		std::string auth;

		int iostatus = 0;
		base64<char> encoder;
		std::back_insert_iterator<std::string> ins = std::back_inserter(auth);
		encoder.put(_authorization.begin(), _authorization.end(), ins, iostatus, base64<>::crlf());

		header += "Authorization: Basic " + auth + "\r\n";
	}
	header += "Content-Type: text/xml\r\nContent-Length: ";

	sprintf(buff,"%i\r\n\r\n", (int) body.size());

	header += buff;

	return header;
}

bool XmlRpcClient::writeRequest()
{
	if (_bytesWritten == 0)
		XmlRpcUtil::log(5, "XmlRpcClient::writeRequest (attempt %d):\n%s\n", _sendAttempts+1, _request.c_str());

	// Try to write the request
	if ( XmlRpcSocket::nbWrite(this->getfd(), _request, &_bytesWritten) != 0 )
	{
		XmlRpcUtil::error("Error in XmlRpcClient::writeRequest: write error (%s).",XmlRpcSocket::getErrorMsg().c_str());
		return false;
	}

	XmlRpcUtil::log(3, "XmlRpcClient::writeRequest: wrote %d of %d bytes.", _bytesWritten, _request.length());

	// Wait for the result
	if (_bytesWritten == int(_request.length()))
	{
		if (_header)
			free(_header);
		_header = NULL;
		_header_length = 0;
		if (_response_buf)
			free(_response_buf);
		_response_buf = NULL;
		_response_length = 0;
		_connectionState = READ_HEADER;
	}
	return true;
}

// Read the header from the response
bool XmlRpcClient::readHeader()
{
	// Read available data
	if ( ! XmlRpcSocket::nbRead(this->getfd(), _header, _header_length, &_eof) ||
		(_eof && _header_length == 0))
	{

		// If we haven't read any data yet and this is a keep-alive connection, the server may
		// have timed out, so we try one more time.
		if (getKeepOpen() && _header_length == 0 && _sendAttempts++ == 0)
		{
			XmlRpcUtil::log(4, "XmlRpcClient::readHeader: re-trying connection");
			XmlRpcSource::close();
			_connectionState = NO_CONNECTION;
			_eof = false;
			return setupConnection();
		}

		XmlRpcUtil::error("Error in XmlRpcClient::readHeader: error while reading header (%s) on fd %d.",
			XmlRpcSocket::getErrorMsg().c_str(), getfd());
		return false;
	}

	XmlRpcUtil::log(4, "XmlRpcClient::readHeader: client has read %d bytes", _header_length);

								 // Start of header
	char *hp = _header;
								 // End of string
	char *ep = hp + _header_length;
	char *bp = 0;				 // Start of body
	char *lp = 0;				 // Start of content-length value
	char *te = 0;				 // Transfer-encoding
	char *co = 0;				 // Connection

	for (char *cp = hp; (bp == 0) && (cp < ep); ++cp)
	{
		if ((ep - cp > 16) && (strncasecmp(cp, "Content-Length: ", 16) == 0))
			lp = cp + 16;
		else if ((ep - cp > 19 ) && (strncasecmp(cp, "Transfer-Encoding: ", 19) == 0))
			te = cp + 19;
		else if ((ep - cp > 12 ) && (strncasecmp(cp, "Connection: ", 12) == 0))
			co = cp + 12;
		else if ((ep - cp > 4) && (strncmp(cp, "\r\n\r\n", 4) == 0))
			bp = cp + 4;
		else if ((ep - cp > 2) && (strncmp(cp, "\n\n", 2) == 0))
			bp = cp + 2;
	}

	// If we haven't gotten the entire header yet, return (keep reading)
	if (bp == 0)
	{
		if (_eof)				 // EOF in the middle of a response is an error
		{
			XmlRpcUtil::error("Error in XmlRpcClient::readHeader: EOF while reading header");
			return false;		 // Close the connection
		}

		return true;			 // Keep reading
	}

	// Decode content length
	if (lp == 0)
	{
		if (te == 0)
		{
			// close connection?
			if (co && strncasecmp (co, "close", 5) == 0)
			{
				_contentLength = -2;
				_chunkLength = -2;
				_chunkReceivedLength = 0;
			}
			else
			{
				XmlRpcUtil::error ("Transfer encoding, lenght or connection close not specified");
				return false;
			}
		}
		if (strncasecmp(te, "chunked", 7) != 0)
		{
			XmlRpcUtil::error("Unknow transfer encoding: %s", te);
			return false;
		}
		_contentLength = -1;
		_chunkLength = -1;
		_chunkReceivedLength = 0;
	}
	else
	{
		_contentLength = atoi(lp);
		if (_contentLength <= 0)
		{
			XmlRpcUtil::error("Error in XmlRpcClient::readHeader: Invalid Content-Length specified (%d).", _contentLength);
			return false;
		}
		XmlRpcUtil::log(4, "client read content length: %d", _contentLength);
	}

	// Otherwise copy non-header data to response buffer and set state to read response.
	_response_length = ep - bp;
	if (_response_buf)
		free (_response_buf);

	_response_buf = (char *) malloc (_response_length);
	memcpy (_response_buf, bp, _response_length);

	_chunkStart = _response_buf;

	if (_header)
		free(_header);
	_header = NULL;				 // should parse out any interesting bits from the header (connection, etc)...
	_header_length = 0;
	_connectionState = READ_RESPONSE;
	return true;				 // Continue monitoring this source
}

bool XmlRpcClient::readResponse()
{
	if (_contentLength == -1 || _contentLength == -2)
	{
		// not full chunk..
		XmlRpcSocket::nbRead(this->getfd(), _response_buf, _response_length, &_eof);
		if (_response_length == 0 && _eof)
		{
			XmlRpcUtil::error ("Error in XmlRpcClient::readResponse: do not received any data");
			return false;
		}
		// wait for close
		if (_contentLength == -2)
		{
			if (!_eof)
			{
				if (_response_length < 50000)
				{
					XmlRpcUtil::error ("Data without content-length longer than 50k - aborting connection");
					return false;
				}
				return true;
			}
			// eof received, connection closed,.. - process data received
		}
		else
		{
			_chunkStart = _response_buf + _chunkReceivedLength;
			while (true)
			{
				for (_chunkEnd = _chunkStart; (_chunkEnd < _response_buf + _response_length) && *_chunkEnd != '\n' && *_chunkEnd != '\0'; _chunkEnd++) {}
				// chunk end not yet found..
				if (_chunkEnd == _response_buf + _response_length + 2)
					return true;
				// get chunkLenght
				*_chunkEnd = '\0';
				char *baseEnd;
				_chunkLength = strtol (_chunkStart, &baseEnd, 16);
				if (baseEnd == NULL || !(*baseEnd == '\0' || *baseEnd == '\r'))
				{
					XmlRpcUtil::error ("Cannot decode chunk length: %s", _chunkStart);
					return false;
				}
				// end of chunk transfer
				if (_chunkLength == 0)
				{
					*_chunkStart = '\0';
					break;
				}
				if (_chunkLength < 0)
				{
					XmlRpcUtil::error ("Error in XmlRpcClient::readResponse: negative chunk length specified (%d)", _chunkLength);
					return false;
				}
				if (_chunkEnd + 1 + _chunkLength > _response_buf + _response_length)
				{
					return true;
				}
				// delete chunk..
				memmove (_chunkStart, _chunkEnd + 1, _response_buf + _response_length - _chunkEnd - 1);
				_response_length -= _chunkEnd + 1 - _chunkStart;
				_chunkStart += _chunkLength;
				_chunkReceivedLength += _chunkLength;
				memmove (_chunkStart, _chunkStart + 2, _response_buf + _response_length - _chunkStart - 2);
				_response_length -= 2;
			}
		}
	}
	// If we dont have the entire response yet, read available data
	else if (int(_response_length) < _contentLength)
	{
		if ( ! XmlRpcSocket::nbRead(this->getfd(), _response_buf, _response_length, &_eof))
		{
			XmlRpcUtil::error("Error in XmlRpcClient::readResponse: read error (%s).",XmlRpcSocket::getErrorMsg().c_str());
			return false;
		}

		// If we haven't gotten the entire _response yet, return (keep reading)
		if (_response_length < _contentLength)
		{
			if (_eof)
			{
				XmlRpcUtil::error("Error in XmlRpcClient::readResponse: EOF while reading response");
				return false;
			}
			return true;
		}
	}

	// Otherwise, parse and return the result
	XmlRpcUtil::log(3, "XmlRpcClient::readResponse (read %d bytes)", _response_length);
	XmlRpcUtil::log(5, "response:\n%s", _response_buf);

	_connectionState = IDLE;

	return false;				 // Stop monitoring this source (causes return from work)
}

// Convert the response xml into a result value
bool XmlRpcClient::parseResponse(XmlRpcValue& result)
{
	std::string _response = std::string (_response_buf);
	// Parse response xml into result
	int offset = 0;
	if ( ! XmlRpcUtil::findTag(METHODRESPONSE_TAG,_response,&offset))
	{
		XmlRpcUtil::error("Error in XmlRpcClient::parseResponse: Invalid response - no methodResponse. Response:\n%s", _response.c_str());
		return false;
	}

	// Expect either <params><param>... or <fault>...
	if ((XmlRpcUtil::nextTagIs(PARAMS_TAG,_response,&offset) &&
		XmlRpcUtil::nextTagIs(PARAM_TAG,_response,&offset)) ||
		(XmlRpcUtil::nextTagIs(FAULT_TAG,_response,&offset) && (_isFault = true)))
	{
		if ( ! result.fromXml(_response, &offset))
		{
			XmlRpcUtil::error("Error in XmlRpcClient::parseResponse: Invalid response value. Response:\n%s", _response.c_str());
			return false;
		}
	}
	else
	{
		XmlRpcUtil::error("Error in XmlRpcClient::parseResponse: Invalid response - no param or fault tag. Response:\n%s", _response.c_str());
		return false;
	}

	return result.valid();
}

void XmlRpcClient::setupProxy()
{
	char *proxy = getenv ("http_proxy");
	if (proxy)
	{
		if (strncmp (proxy, "http://", 7))
			XmlRpcUtil::error("Ignoring HTTP proxy string as it does not start with http://");
		_proxy_host = std::string (proxy + 7);
		size_t dp = _proxy_host.find (':');
		if (dp != std::string::npos)
		{
			_proxy_port = atoi (_proxy_host.substr (dp + 1).c_str ());
			_proxy_host = _proxy_host.substr (0, dp);

		}
		else
		{
			_proxy_port = 80;
		}
	}
	else
	{
		_proxy_port = -1;
	}
}

void XmlRpcClient::setupHost(const char *host, int port, const char *authorization, const char *uri)
{
	XmlRpcUtil::log(1, "XmlRpcClient new client: host %s, port %d.", host, port);

	_header = NULL;
	_header_length = 0;

	_response_buf = NULL;
	_response_length = 0;

	setupProxy ();

	_host = host;
	_port = port;

	if (authorization != NULL)
		_authorization = authorization;
	else
		_authorization = std::string("");
	if (uri != NULL)
		_uri = uri;
	else
		_uri = std::string ("/RPC2");
	if (_proxy_port > 0)
	{
		std::ostringstream _os;
		_os << "http://" << _host << ":" << _port;
		_uri = _os.str () + _uri;
	}
	_connectionState = NO_CONNECTION;
	_executing = NOEXEC;
	_eof = false;

	// Default to keeping the connection open until an explicit close is done
	setKeepOpen();
}
