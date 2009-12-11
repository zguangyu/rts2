/* 
 * Image preview and download classes.
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

#include "imgpreview.h"
#include "../writers/rts2image.h"
#include "bsc.h"
#include "dirsupport.h"

using namespace rts2xmlrpc;

#ifdef HAVE_LIBJPEG

#include <Magick++.h>
using namespace Magick;

void JpegImageRequest::authorizedExecute (std::string path, HttpParams *params, const char* &response_type, char* &response, int &response_length)
{
	response_type = "image/jpeg";
	Rts2Image image (path.c_str (), false, true);
	image.openImage ();
	Blob blob;
	Magick::Image mimage = image.getMagickImage (true);

	mimage.write (&blob, "jpeg");
	response_length = blob.length();
	response = new char[response_length];
	memcpy (response, blob.data(), response_length);
}

void JpegPreview::authorizedExecute (std::string path, HttpParams *params, const char* &response_type, char* &response, int &response_length)
{
	// size of previews
	int prevsize = params->getInteger ("ps", 128);
	// image type
	const char *t = params->getString ("t", "p");
	// if it is a fits file..
	if (path.length () > 6 && (path.substr (path.length () - 5)) == std::string (".fits"))
	{
		response_type = "image/jpeg";

		Rts2Image image (path.c_str (), false, true);
		image.openImage ();
		Blob blob;
		Magick::Image mimage = image.getMagickImage ();
		mimage.zoom (Magick::Geometry (prevsize, prevsize));

		image.writeLabel (mimage, 1, prevsize - 2, 10, "%Y-%m-%d %H:%M:%S");

		mimage.write (&blob, "jpeg");
		response_length = blob.length();
		response = new char[response_length];
		memcpy (response, blob.data(), response_length);
		return;
	}
	std::ostringstream _os;
	_os << "<html><head><title>Preview of " << path << "</title>"
	  << "<style type='text/css'>.normal { border: 5px solid white; } .hig { border: 5px solid navy; }</style></head><body>"
	  << "<script language='javascript'>\n function highlight (name, path) {\n if (document.forms['download'].elements['act'][1].checked)\n { var files = document.getElementById('files'); nc='hig';\n if (document.images[name].className == 'hig')\n { nc='normal'; var i; for (i = files.length - 1; i >=0; i--) { if (files.options[i].value == path) { files.remove(i); i = -1; } } }\nelse\n{\nvar o = document.createElement('option');\no.selected=1;\no.text=path;\no.value=path;\ntry { files.add(o,files.options[0]);\n} catch (ex) { files.add(o,0); }\n }\ndocument.images[name].className=nc;\n }\n else\n { w2 = window.open('" << ((XmlRpcd *)getMasterApp ())->getPagePrefix () << "/jpeg' + path, 'Preview'); w2.focus (); }\n }</script>" << std::endl
	  << "<form name='download' method='post' action='" << ((XmlRpcd *)getMasterApp ())->getPagePrefix () << "/download'><input type='radio' name='act' value='v' checked='checked'>View</input><input type='radio' name='act' value='d'>Download</input>" << std::endl
	  << "<select id='files' name='files' size='10' multiple='multiple' style='display:none'></select><input type='submit' value='Download'/></form>";

	struct dirent **namelist;
	int n;
	int ret;

	const char *pagesort = params->getString ("o", "filename");

	enum {SORT_FILENAME, SORT_DATE} sortby = SORT_FILENAME;
	if (!strcmp (pagesort, "date"))
		sortby = SORT_DATE;

	switch (sortby)
	{
	 	case SORT_DATE:
			/* if following fails to compile, please have a look to value of your
			 * _POSIX_C_SOURCE #define, record it and send it to petr@kubanek.net.
			 * Please contact petr@kubanek.net if you don't know how to get
			 * _POSIX_C_SOURCE. */
			n = scandir (path.c_str (), &namelist, 0, cdatesort);
			break;
		case SORT_FILENAME:
		default:
		  	n = scandir (path.c_str (), &namelist, 0, alphasort);
			break;
	}

	if (n < 0)
	{
		throw XmlRpcException ("Cannot open directory");
	}

	// first show directories..
	_os << "<p>";
	for (int i = 0; i < n; i++)
	{
		char *fname = namelist[i]->d_name;
		struct stat sbuf;
		ret = stat ((path + fname).c_str (), &sbuf);
		if (ret)
			continue;
		if (S_ISDIR (sbuf.st_mode) && strcmp (fname, ".") != 0)
		{
			_os << "<a href='" << ((XmlRpcd *)getMasterApp ())->getPagePrefix () << "/preview" << path << fname << "/?ps=" << prevsize << "'>" << fname << "</a> ";
		}
	}

	_os << "</p><p>";

	// get page number and size of page
	int pageno = params->getInteger ("p", 1);
	int pagesiz = params->getInteger ("s", 40);

	if (pageno <= 0)
		pageno = 1;

	int is = (pageno - 1) * pagesiz;
	int ie = is + pagesiz;
	int in = 0;

	int i;

	for (i = 0; i < n; i++)
	{
		char *fname = namelist[i]->d_name;
		if (strstr (fname + strlen (fname) - 6, ".fits") == NULL)
			continue;
		in++;
		if (in <= is)
			continue;
		if (in > ie)
			continue;
		std::string fpath = std::string (path) + '/' + fname;
		_os
		  << "<img class='normal' name='p" << i << "' onClick='highlight (\"p" << i << "\", \"" << fpath << "\")' width='" << prevsize << "' height='" << prevsize << "' src='" << ((XmlRpcd *)getMasterApp())->getPagePrefix () << "/preview" << fpath << "?ps=" << prevsize << "'/>";
	}

	for (i = 0; i < n; i++)
	{
		free (namelist[i]);
	}

	free (namelist);
			
	// print pages..
	_os << "</p><p>Page ";
	for (i = 1; i <= in / pagesiz; i++)
	 	pageLink (_os, path.c_str (), i, pagesiz, prevsize, i == pageno);
	if (in % pagesiz)
	 	pageLink (_os, path.c_str (), i, pagesiz, prevsize, i == pageno);
	_os << "</p></body></html>";

	response_type = "text/html";
	response_length = _os.str ().length ();
	response = new char[response_length];
	memcpy (response, _os.str ().c_str (), response_length);
}

void JpegPreview::pageLink (std::ostringstream& _os, const char* path, int i, int pagesiz, int prevsize, bool selected)
{
	if (selected)
	{
		_os << "<b>" << i << "</b> ";
	}
	else
	{
		_os << "<a href='" << ((XmlRpcd *)getMasterApp ())->getPagePrefix () << "/preview" << path << "?p=" << i << "&s=" << pagesiz << "&ps=" << prevsize << "'>" << i << "</a> ";
	}
}

#endif /* HAVE_LIBJPEG */

void FitsImageRequest::authorizedExecute (std::string path, HttpParams *params, const char* &response_type, char* &response, int &response_length)
{
	response_type = "image/fits";
	int f = open (path.c_str (), O_RDONLY);
	if (f == -1)
	{
		throw XmlRpcException ("Cannot open file");
	}
	struct stat st;
	if (fstat (f, &st) == -1)
	{
		throw XmlRpcException ("Cannot get file properties");
	}
	response_length = st.st_size;
	response = new char[response_length];
	int ret;
	ret = read (f, response, response_length);
	if (ret != response_length)
	{
		delete[] response;
		throw XmlRpcException ("Cannot read data");
	}
	close (f);
}

void DownloadRequest::authorizedExecute (std::string path, HttpParams *params, const char* &response_type, char* &response, int &response_length)
{
	response_type = "text/html";
	
	std::ostringstream _os;
	_os << "<html><head><title>Download</title></head><body>";

	for (HttpParams::iterator iter = params->begin (); iter != params->end (); iter++)
	{
		_os << iter->getName () << '=' << iter->getValue () << "<br/>\n";
	}

	_os << "</body>";

	response_type = "text/html";
	response_length = _os.str ().length ();
	response = new char[response_length];
	memcpy (response, _os.str ().c_str (), response_length);
}
