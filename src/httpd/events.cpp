/* 
 * Infrastructure which supports events generated by triggers.
 * Copyright (C) 2009 Petr Kubanek <petr@kubanek.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "events.h"

#include "stateevents.h"
#include "message.h"

#include "block.h"
#include "logstream.h"

#include <libxml/parser.h>
#include <libxml/tree.h>

using namespace rts2xmlrpc;

Events::Events (HttpD *_master)
{
	master = _master;
	defImageLabel = NULL;
	docroot = std::string ();
	defchan = INT_MAX;
}

void Events::parseState (xmlNodePtr event, std::string deviceName)
{
	xmlAttrPtr properties = event->properties;
	int changeMask = -1;
	int newStateValue = -1;
	std::string commandName;
	std::string condition;

	for (; properties; properties = properties->next)
	{
		if (xmlStrEqual (properties->name, (xmlChar *) "state-mask"))
		{
			changeMask = atoi ((char *) properties->children->content);
		}
		else if (xmlStrEqual (properties->name, (xmlChar *) "state"))
		{
			newStateValue = atoi ((char *) properties->children->content);
		}
		else if (xmlStrEqual (properties->name, (xmlChar *) "if"))
		{
			condition = std::string ((char *) properties->children->content);
			std::cout << "if " << condition << std::endl;
		}
		else
		{
			throw XmlUnexpectedAttribute (event, (char *) properties->name);
		}
	}
	if (newStateValue < 0)
		throw XmlUnexpectedAttribute (event, "state");

	if (changeMask < 0)
		changeMask = newStateValue;
	
	xmlNodePtr action = event->children;
	if (action == NULL)
		throw XmlEmptyNode (event);

	for (; action != NULL; action = action->next)
	{
		if (action->type == XML_COMMENT_NODE)
		{
			continue;
		}
		else if (xmlStrEqual (action->name, (xmlChar *) "record"))
		{
			stateCommands.push_back (new StateChangeRecord (deviceName, changeMask, newStateValue));
		}
		else if (xmlStrEqual (action->name, (xmlChar *) "command"))
		{
			if (action->children == NULL)
				throw XmlEmptyNode (action);
			
			commandName = std::string ((char *) action->children->content);
			stateCommands.push_back (new StateChangeCommand (deviceName, changeMask, newStateValue, commandName));
		}
		else if (xmlStrEqual (action->name, (xmlChar *) "email"))
		{
			StateChangeEmail *email = new StateChangeEmail (deviceName, changeMask, newStateValue);
			email->parse (action, deviceName.c_str ());
			// add to, subject, body,..
			stateCommands.push_back (email);
		}
		else
		{
			throw XmlUnexpectedNode (action);
		}
	}
}

void Events::parseValue (xmlNodePtr event, std::string deviceName)
{
	std::string condition;

	xmlAttrPtr valueName = xmlHasProp (event, (xmlChar *) "name");
	if (valueName == NULL)
		throw XmlMissingAttribute (event, "name");

	xmlAttrPtr cadencyPtr = xmlHasProp (event, (xmlChar *) "cadency");
	float cadency = -1;
	if (cadencyPtr != NULL)
		cadency = atof ((char *) cadencyPtr->children->content);
	xmlAttrPtr testPtr = xmlHasProp (event, (xmlChar *) "test");
	Expression *test = NULL;
	if (testPtr != NULL)
	  	test = parseExpression ((char *) testPtr->children->content);

	xmlNodePtr action = event->children;
	for (; action != NULL; action = action->next)
	{
		if (action->type == XML_COMMENT_NODE)
		{
			continue;
		}
		else if (xmlStrEqual (action->name, (xmlChar *) "record"))
		{
			valueCommands.push_back (new ValueChangeRecord (master, deviceName, std::string ((char *) valueName->children->content), cadency, test));
		}
		else if (xmlStrEqual (action->name, (xmlChar *) "command"))
		{
			valueCommands.push_back (new ValueChangeCommand (master, deviceName, std::string ((char *) valueName->children->content), cadency, test, std::string ((char *) action->children->content)));
		}
		else if (xmlStrEqual (action->name, (xmlChar *) "email"))
		{
			ValueChangeEmail *email = new ValueChangeEmail (master, deviceName, std::string ((char *) valueName->children->content), cadency, test);
			email->parse (action, deviceName.c_str ());
			// add to, subject, body,..
			valueCommands.push_back (email);
		}
		else
		{
			throw XmlUnexpectedNode (action);
		}
	}
}

void Events::parseMessage (xmlNodePtr event, std::string deviceName)
{
	int nType;

	xmlAttrPtr type = xmlHasProp (event, (xmlChar *) "type");
	if (type == NULL)
		throw XmlMissingAttribute (event, "type");

	if (xmlStrEqual (type->children->content, (xmlChar *) "CRITICAL"))
		nType = MESSAGE_CRITICAL;
	else if (xmlStrEqual (type->children->content, (xmlChar *) "ERROR"))
		nType = MESSAGE_ERROR;
	else if (xmlStrEqual (type->children->content, (xmlChar *) "INFO"))
		nType = MESSAGE_INFO;
	else if (xmlStrEqual (type->children->content, (xmlChar *) "WARNING"))
		nType = MESSAGE_WARNING;
	else if (xmlStrEqual (type->children->content, (xmlChar *) "DEBUG"))
		nType = MESSAGE_DEBUG;
	else
	  	throw XmlUnexpectedAttribute (event, "type");

	xmlNodePtr action = event->children;
	for (; action != NULL; action = action->next)
	{
		if (action->type == XML_COMMENT_NODE)
		{
			continue;
		}
/*		else if (xmlStrEqual (action->name, (xmlChar *) "record"))
		{
			valueCommands.push_back (new ValueChangeRecord (master, deviceName, std::string ((char *) valueName->children->content), cadency, test));
		}
		else if (xmlStrEqual (action->name, (xmlChar *) "command"))
		{
			valueCommands.push_back (new ValueChangeCommand (master, deviceName, std::string ((char *) valueName->children->content), cadency, test, std::string ((char *) action->children->content)));
		} */
		else if (xmlStrEqual (action->name, (xmlChar *) "email"))
		{
			MessageEmail *email = new MessageEmail (master, deviceName, nType);
			email->parse (action, deviceName.c_str ());
			// add to, subject, body,..
			messageCommands.push_back (email);
		}
		else
		{
			throw XmlUnexpectedNode (action);
		}
	}
}

void Events::parseHttp (xmlNodePtr ev)
{
	for (; ev; ev = ev->next)
	{
		if (ev->type == XML_COMMENT_NODE)
		{
			continue;
		}
		else if (xmlStrEqual (ev->name, (xmlChar *) "public"))
		{
			if (ev->children == NULL || ev->children->content == NULL)
				throw XmlMissingElement (ev, "content of public path");
			publicPaths.push_back (std::string ((char *) ev->children->content));
		}
		else if (xmlStrEqual (ev->name, (xmlChar *) "dir"))
		{
			if (ev->children != NULL)
				throw XmlError ("dir node must be empty");

			xmlAttrPtr path = xmlHasProp (ev, (xmlChar *) "path");
			if (path == NULL)
				throw XmlMissingAttribute (ev, "path");

			xmlAttrPtr to = xmlHasProp (ev, (xmlChar *) "to");
			if (to == NULL)
				throw XmlMissingAttribute (ev, "to");

			dirs.push_back (DirectoryMapping ((const char *) path->children->content, (const char *) to->children->content));
		}
		else if (xmlStrEqual (ev->name, (xmlChar *) "docroot"))
		{
			if (ev->children == NULL)
			  	throw XmlEmptyNode (ev);
			if (docroot.length () != 0)
				throw XmlError ("docroot specified more than once");
			docroot = std::string ((const char *) ev->children->content);
		}
		else if (xmlStrEqual (ev->name, (xmlChar *) "default-channel"))
		{
			if (ev->children == NULL)
			  	throw XmlEmptyNode (ev);
			if (defchan != INT_MAX)
				throw XmlError ("docroot specified multiple times");
			defchan = atoi ((char *) ev->children->content);
		}
		else if (xmlStrEqual (ev->name, (xmlChar *) "allsky"))
		{
			if (ev->children == NULL || ev->children->content == NULL)
				throw XmlMissingElement (ev, "content of allsky path");
			allskyPaths.push_back (std::string ((char *) ev->children->content));
		}
		else if (xmlStrEqual (ev->name, (xmlChar *) "defaultImageLabel"))
		{
			if (ev->children == NULL || ev->children->content == NULL)
				throw XmlMissingElement (ev, "content of defaultImageLabel");
			delete[] defImageLabel;
			int l = strlen ((char*) ev->children->content);
			defImageLabel = new char[l + 1];
			memcpy (defImageLabel, ev->children->content, l + 1);
		}
		else
		{
			throw XmlUnexpectedNode (ev);
		}
	}
}

void Events::parseEvents (xmlNodePtr ev)
{
	for (; ev; ev = ev->next)
	{
		if (ev->type == XML_COMMENT_NODE)
		{
			continue;
		}
		else if (xmlStrEqual (ev->name, (xmlChar *) "device"))
		{
			// parse it...
			std::string deviceName;
			std::string commandName;

			// does not have name..
			xmlAttrPtr attrname = xmlHasProp (ev, (xmlChar *) "name");
			if (attrname == NULL)
			{
				throw XmlMissingAttribute (ev, "name");
			}
			deviceName = std::string ((char *) attrname->children->content);

			for (xmlNodePtr event = ev->children; event != NULL; event = event->next)
			{
				// switch on action
				if (event->type == XML_COMMENT_NODE)
				{
					continue;
				}
				else if (xmlStrEqual (event->name, (xmlChar *) "state"))
				{
					parseState (event, deviceName);
				}
				else if (xmlStrEqual (event->name, (xmlChar *) "value"))
				{
					parseValue (event, deviceName);
				}
				else if (xmlStrEqual (event->name, (xmlChar *) "message"))
				{
					parseMessage (event, deviceName);	
				}
				else
				{
					throw XmlUnexpectedNode (event);
				}
			}
		}
		else
		{
			throw XmlUnexpectedNode (ev);
		}
	}
}

void Events::parseBB (xmlNodePtr ev)
{
#ifdef RTS2_JSONSOUP
	if (ev == NULL)
		throw XmlMissingElement (ev, "server description for BB");
	
	char *sn = NULL;
	char *password = NULL;
	int oid = -1;
	int cadency = 60;

	for (xmlNodePtr chil = ev->children; chil; chil = chil->next)
	{
		if (chil->type == XML_COMMENT_NODE)
		{
			continue;
		}
		else if (xmlStrEqual (chil->name, (xmlChar *) "server"))
		{
			sn = (char *)chil->children->content;
		}
		else if (xmlStrEqual (chil->name, (xmlChar *) "observatory"))
		{
			oid = atoi ((char *)chil->children->content);
		}
		else if (xmlStrEqual (chil->name, (xmlChar *) "password"))
		{
			password = (char *)chil->children->content;
		}
		else if (xmlStrEqual (chil->name, (xmlChar *) "cadency"))
		{
			cadency = atoi ((char *)chil->children->content);
		}
		else throw XmlUnexpectedNode (chil);
	}
	if (sn == NULL)
		throw XmlMissingElement (ev, "server name");
	if (oid < 0)
		throw XmlMissingElement (ev, "observatory name");
	if (password == NULL)
		throw XmlMissingElement (ev, "password");
	bbServers.push_back (BBServer (master, sn, oid, password, cadency));
#else
	throw rts2core::Error ("missing BB server support (missing soup and json libraries?");
#endif	
}

void Events::load (const char *file)
{
	stateCommands.clear ();
	valueCommands.clear ();
	publicPaths.clear ();
	allskyPaths.clear ();

	defImageLabel = NULL;

	xmlDoc *doc = NULL;
	xmlNodePtr root_element = NULL;

	LIBXML_TEST_VERSION

	xmlLineNumbersDefault (1);

	doc = xmlReadFile (file, NULL, XML_PARSE_NOBLANKS);
	if (doc == NULL)
	{
		logStream (MESSAGE_ERROR) << "cannot parse XML file " << file << sendLog;
		return;
	}

	root_element = xmlDocGetRootElement (doc);

	if (strcmp ((const char *) root_element->name, "config"))
		throw XmlUnexpectedNode (root_element);

	// traverse triggers..
	xmlNodePtr ev = root_element->children;
	if (ev == NULL)
	{
		logStream (MESSAGE_WARNING) << "no device specified" << sendLog;
		return;
	}
	for (; ev; ev = ev->next)
	{
		if (ev->type == XML_COMMENT_NODE)
		{
			continue;
		}
		else if (xmlStrEqual (ev->name, (xmlChar *) "http"))
		{
			parseHttp (ev->children);
		}
		else if (xmlStrEqual (ev->name, (xmlChar *) "events"))
		{
			parseEvents (ev->children);
		}
		else if (xmlStrEqual (ev->name, (xmlChar *) "bb"))
		{
			parseBB (ev);
		}
		else
		{
			throw XmlUnexpectedNode (ev);
		}
	}

	xmlFreeDoc (doc);
	xmlCleanupParser ();
}

bool Events::isPublic (std::string path)
{
	for (std::vector <std::string>::iterator iter = publicPaths.begin (); iter != publicPaths.end (); iter++)
	{
		// ends with *
		int l = iter->length () - 1;
		if ((*iter)[l] == '*')
		{
			if (path.substr (0, l - 1) == iter->substr (0, l - 1))
				return true;
		}
		else
		{
			if (path == *iter)
				return true;
		}
	}
	return false;
}