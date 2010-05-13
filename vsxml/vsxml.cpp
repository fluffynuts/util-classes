#include "vsxml.h"

std::vector<QuoteTranslation> VSXML::QuoteTranslations;								// declaration of static private vector
#ifndef WIN32
#ifndef strlwr
#define strlwr(sz) \
	for (size_t i = 0; i < strlen(sz); i++) \
	{ \
		if ((sz[i] >= 'A') && (sz[i] <= 'Z')) \
			sz[i] += ('a' - 'A'); \
	}
#endif		
#endif

#ifdef WIN32
const char *vsxml_stristr(const char *sz1, const char *sz2)
#else
char *vsxml_stristr(const char *sz1, const char *sz2)
#endif
{
  char *nsz1 = (char *)malloc((1 + strlen(sz1)) * sizeof(char));
  strcpy(nsz1, sz1);
  strlwr(nsz1);
  char *nsz2 = (char *)malloc((1 + strlen(sz2)) * sizeof(char));
  strcpy(nsz2, sz2);
  strlwr(nsz2);
  
  const char *ret = strstr(nsz1, nsz2);
  if (ret)  // now make the search relative to the input char *'s
    ret = sz1 + ((strlen(sz1) - strlen(ret)) * sizeof(char));
  free(nsz1);
  free(nsz2);
  
  #ifdef WIN32
  return ret;
  #else
  return (char *)(ret);
  #endif
}

int vsxml_istrcmp(const char *sz1, const char *sz2)
{
  #ifdef WIN32
  return stricmp(sz1, sz2);
  #else
  return strcasecmp(sz1, sz2);
  #endif
}

VSXML::VSXML()
{
  this->Init();
}

/*
VSXML::VSXML(const CHAR *szName)
{
  this->name = szName;
  this->Init();
}
*/

void VSXML::Init()
{
	this->self = this;													// set pointer to self for comparison
	this->parent = NULL;												// set no parent
	this->CaseSensitive = true;									// set case sensitivity on
	this->AllowCondensedNodes = true;						// allow conforming condensed node format
	this->IsComment = false;                    // default is non-comment node
	if (this->QuoteTranslations.size() == 0)		// if the static array isn't loaded, load now
		this->InitQuoteTranslations();
  this->Condensed = false;                  // don't condense the output
  this->WhiteSpaceMatters = false;          // leading / trailing whitespace and multiple newlines are removed
  this->IncludeXMLSpec = false;               // if unchanged, the effect of this is re-evaluated at render time
  this->IsQNode = false;                      // default is regular node
  this->SuppressComments = false;               
	this->NoisyDeath = false;									// bleat when being destroyed
	this->AutoDeleteMe = true;								// parent should delete this child at destruction time
}

VSXML::~VSXML()
{
	this->Clear();														// release mem
	if (this->NoisyDeath)
		printf("VSXML %lx dying\n", (unsigned long)this);
}

void VSXML::Log(std::string str)
{
	this->ErrorLog.push_back(str);										// log an error
}

void VSXML::ClearErrors()
{
	this->ErrorLog.clear();												// clear error log
}

std::string VSXML::LastError()
{
  if (this->ErrorLog.size() == 0)
    return "";
  return this->ErrorLog[this->ErrorLog.size() - 1];
}

bool VSXML::operator==(VSXML *compare)									// check for exact match of pointers
{
	if (compare->self == this->self)
		return true;
	else
		return false;
}

void VSXML::Clear()
{
  for (size_t i = 0; i < this->Children.size(); i++)
		if (this->Children[i]->AutoDeleteMe)
	    delete this->Children[i];
	this->Children.clear();												// remove all children
	this->Attributes.clear();											// remove all attributes

	this->ClearErrors();												// clear all errors
}

void VSXML::AddComment(std::string strComment)
{
  VSXML *comment = this->AddChild();
  comment->IsComment = true;
  comment->SetText(strComment);
}

void VSXML::CompressNewlines(std::string *in)
{
  if (this->WhiteSpaceMatters)
    return;
  std::string strDoubleNewline = NEWLINE;
  strDoubleNewline += NEWLINE;
	while (in->find(strDoubleNewline.c_str()) != std::string::npos)							// whilst there are double newlines, keep on killing them
	{
		*in = this->strReplace(*in, strDoubleNewline, NEWLINE);
	}
}

std::string VSXML::strReplace(std::string haystack, std::string needle, std::string newneedle)
{
	size_t pos;
	size_t lastPos = 0;
	std::string retVal = "";
	do
	{
		pos = haystack.find(needle);												// look for first occurrence of string
		if (pos == std::string::npos)
		{
			retVal.append(haystack.substr(lastPos, haystack.length() - lastPos)); 	// no match -- put the rest of the string on the return value
		}
		else
		{
			retVal.append(haystack.substr(0, pos - lastPos));						// match; add stuff before needle to return value
			retVal.append(newneedle);												// add new needle
			haystack = haystack.substr(pos + needle.length(), haystack.length() - needle.length() - pos);	// reduce haystack
		}
	} while (pos != std::string::npos);
	
	return retVal;
}

bool VSXML::ParseFile(std::string filename)
{
	this->Clear();																	// make sure we are clear
	struct stat st;																	// stat struct to check on file
	if (stat(filename.c_str(), &st) == 0)
	{
		FILE *fp;
		fp = fopen(filename.c_str(), "rb");											// try to open file
		 
		if (fp != NULL)
		{
			char *buf = new char[st.st_size + 1];									// holds file contents
      size_t stToRead, stRead = 0, stTmp;
      char *ptr = buf;
      while (stRead < (size_t)st.st_size)
      {
        stToRead = st.st_size - stRead;
        if (stToRead > 4096)
          stToRead = 4096;
        if ((stTmp = fread(ptr, sizeof(char), stToRead, fp)) != stToRead)
        {
          if (fclose(fp))
            errno = 0;
          this->Log("Unable to fully read file");
          delete[] buf;
          return false;
        }
        stRead += stToRead;
        ptr += stToRead * sizeof(char);
      }
      buf[st.st_size] = '\0';
			std::string contents;														// holds contents
			for (unsigned int i = 0; i < (unsigned int)st.st_size; i++)
				contents += buf[i];													// read contents into string (could be done better)
      delete[] buf;
			contents = this->Trim(contents);										// remove leading and trailing whitespace
			return this->ParseString(&contents);									// load from string
		}
		else																		// file open fails
		{
			this->Log("Can't open file '" + filename + "' for reading: " + this->strErr(errno));
			errno = 0;
			return false;
		}

	}
	else																			// file stat fails
	{
		this->Log("Can't stat file '" + filename + "'" + strErr(errno));
		errno = 0;
		return false;
	}
}

bool VSXML::ParseString(std::string in)
{
	return this->ParseString(&in);													// load from string (pointer version)
}

bool VSXML::ParseString(std::string *in)
{
	std::string innerContent, strTmp, textNode, strTag;
	VSXML *child;
	size_t pos1 = 0, pos2, innerContentStart, innerContentEnd;
	std::vector<std::string> tmp1, tmp2;
	xmlMetaTag tmpMeta;
	xmlAttrib tmpAttrib;

	while ((pos1 = in->find("<?", pos1) != std::string::npos))							// strip out leading  <?xml ?> descriptor
	{
		pos2 = in->find("?>", pos1);
		if (pos2 == std::string::npos)
		{
			this->Log("ERROR: Malformed XML: xml specification is not terminated, and since I can't tell where the useful data starts, I have to exit");
			return false;
		}
		std::string xmlMeta = in->substr(pos1 + 2, pos2 - pos1 - 2);
		*in = in->substr(0, pos1 - 1) + in->substr(pos2 + 2, in->length() - pos2 - 2);
		tmp1 = this->Split(&xmlMeta, " ", true);
		tmpMeta.name = tmp1[0];
		for (unsigned int i = 1; i < tmp1.size(); i++)
		{
			if (tmp1[i].find("=") != std::string::npos)
			{
				tmp2 = this->Split(&(tmp1[i]), "=", true);				// split attributes into name and value pairs
				tmpAttrib.name = this->Trim(tmp2[0]);
				tmpAttrib.value = this->Trim(tmp2[1], " \n\r\t\"");
			}
			else
			{
				tmpAttrib.name = this->Trim(tmp1[i], " \n\r\t\"");
				tmpAttrib.value = "";
			}
			tmpMeta.Attributes.push_back(tmpAttrib);					// add meta attribs
		}
		this->MetaTags.push_back(tmpMeta);
	}

	strTag = this->GetFirstTagName(in);									// get starting tag name
	this->name = strTag;
	pos1 = in->find("<" + strTag) + 1 + strTag.length();
	pos2 = in->find(">", pos1);
	strTmp = this->Trim(this->Trim(in->substr(pos1, pos2 - pos1)), "/");
	if (strTmp.length())												// if we have something to work with
	{
		tmp1 = this->Split(&strTmp, " ", true);
		for (unsigned int i = 0; i < tmp1.size(); i++)
		{
			if (tmp1[i].find("=") != std::string::npos)						// split attributes into name/value pairs
			{
				tmp2 = this->Split(&(tmp1[i]), "=", true);
				tmpAttrib.name = this->Trim(tmp2[0], "\"");
				tmpAttrib.value = this->Trim(tmp2[1], "\"");
				tmpAttrib.noValue = false;
			}
			else														// this attribute has no value
			{
				tmpAttrib.name = this->Trim(tmp1[i], "\"");
				tmpAttrib.value = "";
				tmpAttrib.noValue = true;
			}
			this->Attributes.push_back(tmpAttrib);						// add attribute
		}
	}

	innerContentStart = pos2 + 1;										// slice out inner content
	innerContentEnd = this->GetCloseTagPos(in, innerContentStart, strTag) - 1;
	innerContent = this->Trim(in->substr(innerContentStart, 
		innerContentEnd - innerContentStart + 1));
	pos1 = in->find(">", innerContentEnd);
	if (pos1 == std::string::npos)
		pos1 = in->length();
	*in = in->substr(pos1);

	while (innerContent.length())										// parse out children and text nodes
	{
		pos1 = innerContent.find("<");
		if (pos1 == std::string::npos)										// trivial: text node only
		{
		  if (!this->WhiteSpaceMatters)
		    innerContent = this->Trim(innerContent);
			this->TextNodes.push_back(innerContent);
			innerContent = "";
		}
		else															// get leading text node
		{
			textNode = innerContent.substr(0, pos1);
			if (textNode.length())
			{
			  if (!this->WhiteSpaceMatters)
			    innerContent = this->Trim(innerContent);
				innerContent = innerContent.substr(pos1);
				this->TextNodes.push_back(textNode);
			}
			
			strTag = this->GetFirstTagName(&innerContent);				// slice off first child
			pos1 = innerContent.find("<" + strTag);
			pos2 = this->GetCloseTagPos(&innerContent, 
				pos1 + strTag.length() + 1, strTag);
			pos2 = innerContent.find(">", pos2);
			if (pos2 == std::string::npos)
				pos2 = innerContent.length();
			else
				pos2++;

			strTmp = this->Trim(innerContent.substr(pos1, pos2 - pos1));
			innerContent = this->Trim(innerContent.substr(pos2));

			child = this->AddChild();									// add a child node to ourselves
			child->CaseSensitive = this->CaseSensitive;					// set same case sensitivity as we have
			child->ParseString(strTmp);									// get child node to parse string
		}
	}

	return true;
}

size_t VSXML::GetCloseTagPos(std::string *content, size_t start, 
	std::string tag)
{
	size_t pos1, openCount;

	std::string closetag = "</" + tag;
	pos1 = content->find(closetag, start);							// look for close of tag
	if (pos1 == std::string::npos)
	{
		this->Log("Can't find close tag (1) for \"" + tag + "\" in string: \""
			+ *content + "\"");
		return content->length();
	}

	openCount = this->StringCount(content, "<" + tag, start, pos1);

	while (openCount)												// look for close of tag relative to the one we're interested in (not children inside the node)
	{
		pos1 = content->find(closetag, pos1 + 1);
		if (pos1 == std::string::npos)
		{
			return content->length();
			this->Log("Can't find close tag (2) for \"" + tag 
				+ "\" in string: \"" + *content + "\"");
		}
		openCount--;
	}
	
	return pos1;
}

std::string VSXML::GetFirstTagName(std::string *content)
{
	size_t pos1, pos2, pos3;

	pos1 = content->find("<");				// look for first open tag
	if (pos1 == std::string::npos)
		return "";
	pos2 = content->find(" ", pos1);
	pos3 = content->find(">", pos1);		// get end of tag
	if (pos3 < pos2)
		pos2 = pos3;
	
	return content->substr(pos1 + 1, pos2 - pos1 - 1);	// extract tag name
}

VSXML *VSXML::AddChild(std::string strName)
{
	VSXML *newchild = new VSXML();							// create child object
	long long idx = this->AddChild(newchild);		// add child
	if (idx > -1)
	{
	  this->Children[(size_t)idx]->parent = this;
	  this->Children[(size_t)idx]->name = strName;
	  this->Children[(size_t)idx]->AllowCondensedNodes = this->AllowCondensedNodes;
		return this->Children[(size_t)idx];		// return child
  }
	else
		return NULL;						// problem
}

long long VSXML::AddChild(VSXML *node)
{
	try
	{
		this->Children.push_back(node);		// add child node to known array
		node->WhiteSpaceMatters = this->WhiteSpaceMatters;
    node->Condensed = this->Condensed;
		node->SuppressComments = this->SuppressComments;
		return (long long)this->Children.size() - 1;
	}
	catch (std::exception &e)
	{
		std::string err = e.what();
		this->Log("Unable to add child node: " + err);
		return -1;
	}
}

bool VSXML::DelChild(VSXML *node)
{
	for (unsigned int i = 0; i < this->Children.size(); i++)
	{
		if (this->Children[i] == node)
		{
			if (this->Children[i]->AutoDeleteMe)
				delete this->Children[i];
			this->Children.erase(this->Children.begin() + i);

			return true;
		}
	}
	return false;
}

bool VSXML::DelChild(unsigned int idx)
{
	if (idx < this->Children.size())
	{
		if (this->Children[idx]->AutoDeleteMe)
			delete this->Children[idx];
		this->Children.erase(this->Children.begin() + idx);
		return true;
	}
	else
	{
		std::string err;
		err = "ERROR: Attempt to remove child past end of vector (" 
			+ this->llToStr(idx) + ")";
		this->Log(err);
		return false;
	}
}

bool VSXML::HasAttrib(std::string name)
{
	if (this->GetAttribIDX(name) == -1)							// if the attrib idx is > -1; then it exists
		return false;
	else
		return true;
}

int VSXML::GetAttribIDX(std::string name)							// for now, just do a linear search. A binary search *could* be implemented
{	
	if (this->CaseSensitive)
	{
		for (unsigned int i = 0; i < this->Attributes.size(); i++)
		{
				if (strcmp(this->Attributes[i].name.c_str(), name.c_str()) == 0)		// we have a match for the attribute
					return i;
		}
	}
	else
	{
		for (unsigned int i = 0; i < this->Attributes.size(); i++)
		{
				if (vsxml_istrcmp(this->Attributes[i].name.c_str(), name.c_str()) == 0)	// we have a match for the attribute
					return i;
		}
	}
	return -1;													// no match found
}

std::string VSXML::GetAttrib(std::string name, std::string defaultval)
{
	int idx;
	if ((idx = this->GetAttribIDX(name)) == -1)					// get attribute idx, if possible
		return defaultval;										// attrib not found, return default value
	return this->Attributes[idx].value;							// return attribute value
}

void VSXML::SetAttrib(std::string name, std::string value)
{
	int idx = this->GetAttribIDX(name);							// get existing attrib idx
	if (idx == -1)												// add new attrib
	{
		xmlAttrib tmp;
		tmp.name = name;
		tmp.value = value;
		tmp.noValue = false;
		this->Attributes.push_back(tmp);
	}
	else
	{
		this->Attributes[idx].value = value;					// update existing attrib
		this->Attributes[idx].noValue = false;
	}
}

void VSXML::SetNoValAttrib(std::string name)
{
	int idx = this->GetAttribIDX(name);							// get existing attrib idx
	if (idx == -1)												// add new attrib
	{
		xmlAttrib tmp;
		tmp.name = name;
		tmp.value = "";
		tmp.noValue = true;
		this->Attributes.push_back(tmp);
	}
	else
	{
		this->Attributes[idx].value = "";					// update existing attrib
		this->Attributes[idx].noValue = true;
	}
}

void VSXML::DelAttrib(std::string name)
{
	int idx = this->GetAttribIDX(name);							// get existing attrib idx
	if (idx > -1)
		this->Attributes.erase(this->Attributes.begin() + idx);
}

void VSXML::SetAttrib(std::string name, int value)
{
  CHAR sz[64];
  sprintf(sz, "%i", value);
  this->SetAttrib(name, sz);
}

void VSXML::SetAttrib(std::string name, double value , int precision)
{
  CHAR *sz = new char[64 + precision];
  CHAR format[8];
  sprintf(format, "%%.%if", precision);
  sprintf(sz, format, value);
  this->SetAttrib(name, sz);
  delete[] sz;
}

std::string VSXML::RenderToString(unsigned int indent)
{
  if (this->IsComment && this->SuppressComments)
    return "";
  std::string retVal;
  bool fMakeHumanReadable = !(this->Condensed);
  if (this->parent == NULL)   // root node
  {
    this->IsComment = false;      // root node may NEVER be a comment (simplified)
    if (this->IncludeXMLSpec || (strcmp(this->name.c_str(), "html") == 0) || (strcmp(this->name.c_str(), "HTML") == 0))
	    retVal = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
	    
    CHAR *szName = (CHAR *)malloc((1 + this->name.length()) * sizeof(CHAR));
    strcpy(szName, this->name.c_str());
    strlwr(szName);
    if (strcmp(szName, "html") == 0)
    {
      if (fMakeHumanReadable)
        retVal += NEWLINE;
      retVal += "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">";
    }
		free(szName);
    
	  if (fMakeHumanReadable)
	    retVal += NEWLINE;
	}

  if (fMakeHumanReadable)
	  for (unsigned int i = 0; i < indent; i++)					// indent, to make a little more readable
		  retVal += "  ";
  if (this->IsComment)
  {
    retVal += "<!-- " + strReplace(this->Text(), "--", "::") + " -->";
    if (fMakeHumanReadable)
      retVal += NEWLINE;
    return retVal;
  }
  
	if (this->IsQNode)
  	retVal += "<?" + this->name;
	else
  	retVal += "<" + this->name;
	
	for (unsigned int i = 0; i < this->Attributes.size(); i++)	// output attributes
	{
		if (this->Attributes[i].noValue)
			retVal += " " + this->Attributes[i].name;
		else
			retVal += " " + this->Attributes[i].name + "=\""
				+ this->Attributes[i].value+ "\"";
	}
	
	if (this->IsQNode)
	{
	  retVal += "?>";
	  return retVal;
	}
	else
	{
	  if (this->AllowCondensedNodes && (this->Children.size() == 0) && (this->TextNodes.size() == 0))	// condensed node format
	  {
		  retVal += "/>";
		  if (fMakeHumanReadable)
		    retVal += NEWLINE;
		  this->CompressNewlines(&retVal);							// remove extraneous newlines
		  return retVal;
	  }
  	
	  retVal += ">";
	}
	if (fMakeHumanReadable)
	  retVal += NEWLINE;

	for (unsigned int i = 0; i < this->TextNodes.size(); i++)	// add text nodes
	{
	  if (fMakeHumanReadable)
	  {
		  for (unsigned int j = 0; j <= indent; j++)
			  retVal += "  ";
		  if (i > 0)
			  retVal += NEWLINE;
		}
		retVal += this->Trim(this->TextNodes[i]);
	}

	for (unsigned int i = 0; i < this->Children.size(); i++)	// get all children to render themselves
	{
	  if (fMakeHumanReadable)
		  retVal += NEWLINE;
		if (this->WhiteSpaceMatters)
		  this->Children[i]->SetWhiteSpaceMatters();
		else
		  this->Children[i]->Condensed = this->Condensed;
		retVal += this->Children[i]->RenderToString(indent + 1);
	}
	if (fMakeHumanReadable)
	{
	  if (retVal[retVal.length() - 1] != '\n')
		  retVal += NEWLINE;
	  for (unsigned int i = 0; i < indent; i++)
		  retVal += "  ";
  }
	retVal += "</" + this->name + ">";						// close our tag
	if (fMakeHumanReadable)
	  retVal += NEWLINE;

	this->CompressNewlines(&retVal);							// remove extraneous newlines

	return retVal;
}

bool VSXML::RenderToFile(std::string filename, bool overwrite)
{
	struct stat st;

	if (stat(filename.c_str(), &st) == 0)						// check if file exists
	{
		if (st.st_mode & S_IFREG)
		{
			if (!overwrite)										// fail if file exists and overwrite not set
			{
				this->Log("'" + filename + "' already exists, and overwrite was not set on");
				return false;
			}
		}
		else
		{
			this->Log("Can't write to non-regular file '" + filename + "'");	// can't write to file
			return false;
		}
	}
	else
		errno = 0;


	FILE *fp = fopen(filename.c_str(), "wb");
	
	if (fp == NULL)										// check file is open
	{
		this->Log("Unable to open file '" + filename + "' for writing");
		errno = 0;
		return false;
	}

	std::string tmp = this->RenderToString();						// render to temp string
  
  size_t stToWrite, stWritten, stLen = tmp.length();
  const char *pos = tmp.c_str();
  bool ret = true;
  while (stLen)
  {
    stToWrite = stLen;
    if (stToWrite > FILE_WRITE_CHUNKSIZE)
      stToWrite = FILE_WRITE_CHUNKSIZE;
    if ((stWritten = fwrite(pos, sizeof(char), stToWrite, fp)) != stToWrite)
    {
      fprintf(stderr, "VSXML: failed to write %zu bytes at offset %zu to %s\n",
        stToWrite, (tmp.length() - stLen), filename.c_str());
      ret = false;
      break;
    }
    stLen -= stWritten;
    pos += (stWritten * sizeof(char));
  }
	//size_t bytesWritten = fwrite(tmp.c_str(), sizeof(char), tmp.length(), fp);
	
	if (fclose(fp))
		errno = 0;
	
	/*
	if (bytesWritten != tmp.length())
	{
		std::string log;
		log = "Unable to write all of rendered xml string to file: wrote ";
		log += this->llToStr(bytesWritten) + " of " + this->llToStr(tmp.length());
		this->Log(log);
		return false;
	}
	*/
	return ret;
}

std::vector<std::string> VSXML::Split(std::string *str, std::string delimiter, bool dontBreakQuotes, bool trimElements)
{
	std::vector<std::string> retVal;
	size_t lastPos = 0, matchPos;
	std::string tmp;

	if (str->length() == 0)
	{
		retVal.push_back("");
		return retVal;
	}
	matchPos = str->find(delimiter.c_str(), lastPos);		// look for delimiter
	while (matchPos != std::string::npos)
	{
		tmp = str->substr(lastPos, matchPos - lastPos);		// slice off part
		lastPos = matchPos + delimiter.length();			// go past delimiter
		if (dontBreakQuotes)
		{
			while (StringCount(&tmp, "\"") % 2)				// handle quoting
			{
				tmp += str->at(lastPos - 1); 				// get chucked delimiter string
				matchPos = str->find(delimiter.c_str(), lastPos);
				if (matchPos == std::string::npos)
				{
					tmp += str->substr(lastPos, str->length() - lastPos);
					lastPos = str->length();
					break;
				}
				else
				{
					tmp += str->substr(lastPos, matchPos - lastPos);
					lastPos = matchPos + delimiter.length();
				}
			}
		}
		retVal.push_back(tmp);
		matchPos = str->find(delimiter.c_str(), lastPos);
	}
	if (lastPos > 0)										// pick up the straggler part
	{
		if (lastPos < str->length())
		{
			retVal.push_back(str->substr(lastPos, str->length() - lastPos));
		}
	}
	else 													// trivial case: delimiter not found, return 1 element array
    retVal.push_back(*str);
  if (trimElements)
  {
    for (size_t i = 0; i < retVal.size(); i++)
      retVal[i] = this->Trim(retVal[i]);
  }
	return retVal;
}

size_t VSXML::StringCount(std::string *haystack, std::string needle,
	size_t start, size_t end)
{
	if (end == 0)
		end = haystack->length();								// default end is end of the haystacl\k
	size_t pos = haystack->find(needle, start);			// find needle
	if (pos == std::string::npos)
		return 0;												// didn't find any occurrences
	if (pos > (unsigned int)end)
		return 0;												// only occurrence(s) are after set end pos
	
	unsigned int retVal = 0;
	while ((pos < (unsigned int)end) && (pos != std::string::npos))	// whilst we can find needle, increment counter for needle
	{
		retVal++;
		pos = haystack->find(needle, pos + needle.length());
	}

	return retVal;
}

std::string VSXML::Trim(std::string str, std::string trimchars)
{
	long long llLoop, llInnerLoop, llLeftPos = 0, llRightPos = (long long)str.length();
	bool foundMatch;

	for (llLoop = 0; llLoop < (int)str.length(); llLoop++)				// look for right-most left position of last trim char
	{
		foundMatch = false;
		for (llInnerLoop = 0; llInnerLoop < (int)trimchars.length(); llInnerLoop++)
		{
			if (str[(size_t)llLoop] == trimchars[(size_t)llInnerLoop])
			{
				llLeftPos++;
				foundMatch = true;
				break;
			}
		}
		if (!foundMatch) break;
	}

	for (llLoop = (long long)str.length() - 1; llLoop > -1; llLoop--)				// look for left-most right position of last trim char
	{
		foundMatch = false;
		for (llInnerLoop = 0; llInnerLoop < (int)trimchars.length(); llInnerLoop++)
		{
			if (str[(size_t)llLoop] == trimchars[(size_t)llInnerLoop])
			{
				llRightPos--;
				foundMatch = true;
				break;
			}
		}
		if (!foundMatch) break;
	}

	if (llLeftPos < llRightPos)
	{
		return str.substr((size_t)llLeftPos, (size_t)(llRightPos - llLeftPos));
	}
	else
	{
		return "";
	}
}

std::string VSXML::Text()
{
	std::string retVal = "";
	std::string tmp;
	for (unsigned int i = 0; i < this->TextNodes.size(); i++)		// concat all text nodes
	{
    if (!this->WhiteSpaceMatters)
  		tmp = this->Trim(this->TextNodes[i]);
    else
      tmp = this->TextNodes[i];

		if (tmp.length() == 0)
			continue;

		if (retVal.length())
			retVal += NEWLINE;
		retVal += tmp;
	}
	return this->Dequote(retVal);									// remove XML quoting
}

void VSXML::AddText(std::string str, bool blnQuote)
{
  if (blnQuote)
	  str = this->Enquote(str);										// xml quote string
	this->TextNodes.push_back(str);									// add text node
}

void VSXML::SetText(std::string str, bool blnQuote)
{
  if (blnQuote)
	  str = this->Enquote(str);										// xml quote string
	this->TextNodes.clear();										// clear current text
	this->TextNodes.push_back(str);									// add string
}

std::string VSXML::Enquote(std::string str)
{
	for (unsigned int i = 0; i < this->QuoteTranslations.size(); i++)	// replace all known unquoted translations with quoted ones
	{
		str = this->strReplace(str, this->QuoteTranslations[i].UnquotedVal, 
			this->QuoteTranslations[i].QuotedVal);
	}
	return str;
}

std::string VSXML::Dequote(std::string str)
{
	for (unsigned int i = 0; i < this->QuoteTranslations.size(); i++)	// replace all known quoted translations with unquoted ones
	{
		str = this->strReplace(str, this->QuoteTranslations[i].QuotedVal, 
			this->QuoteTranslations[i].UnquotedVal);
	}
	return str;
}

void VSXML::InitQuoteTranslations()										// load map of xml quotations
{
	QuoteTranslation tmp;
	tmp.QuotedVal = "&gt;";
	tmp.UnquotedVal = ">";
	this->QuoteTranslations.push_back(tmp);
	tmp.QuotedVal = "&lt;";
	tmp.UnquotedVal = "<";
	this->QuoteTranslations.push_back(tmp);
	tmp.QuotedVal = "&amp;";
	tmp.UnquotedVal = "&";
	this->QuoteTranslations.push_back(tmp);
	tmp.QuotedVal = "&quot;";
	tmp.UnquotedVal = "\"";
	this->QuoteTranslations.push_back(tmp);
}

bool VSXML::HasChild(std::string NodeName)
{
	if (this->ChildCount(NodeName))										// we have at least one child by this name
		return true;
	else
		return false;													// no children by this name
}

size_t VSXML::ChildCount(std::string NodeName)
{
	if (NodeName.length())
	{
		unsigned int ret = 0;											// current child count
		if (this->CaseSensitive)
		{
			for (unsigned int i = 0; i < this->Children.size(); i++)
			{
				if (strcmp(this->Children[i]->name.c_str(), NodeName.c_str()) == 0)	// found a child match
				{	
					ret++;												// increment counter
				}
			}
		}
		else
		{
			for (unsigned int i = 0; i < this->Children.size(); i++)
			{
				if (vsxml_istrcmp(this->Children[i]->name.c_str(), NodeName.c_str()) == 0)	// found a child match
				{
					ret++;												// increment counter
				}
			}
		}

		return ret;
	}
	else
		return this->Children.size();									// no name given returns total child count
}

VSXML* VSXML::GetChildByName(std::string NodeName, unsigned int idx)
{
	unsigned int matches = 0;
	if (this->CaseSensitive)
	{
		for (unsigned int i = 0; i < this->Children.size(); i++)
		{
			if (strcmp(this->Children[i]->name.c_str(), NodeName.c_str()) == 0)	// found matching child name
			{
				if (matches == idx)												// found matching child count
					return this->Children[i];								// return child
				else
					matches++;													// look for next match
			}
		}
	}
	else
	{
		for (unsigned int i = 0; i < this->Children.size(); i++)
		{
			if (vsxml_istrcmp(this->Children[i]->name.c_str(), NodeName.c_str()) == 0) // found matching child name
			{
				if (matches == idx)												// found matching child count
					return this->Children[i];								// return child
				else
					matches++;													// look for next match
			}
		}
	}
	return NULL;																// this is what you get when you ask for what I don't have  (:
}

std::string VSXML::GetTextChildContents(std::string NodeName, std::string defaultVal, unsigned int idx)
{
	VSXML *child = this->GetChildByName(NodeName);
	if (child == NULL)															// can't find child
	{
		this->Log("Can't find child by name: \"" + NodeName 
			+ "\"; using default value for text node (\"" + defaultVal + "\")");
		return defaultVal;
	}
	else
	{
		return child->Text();													// get child text contents
	}
}

std::string VSXML::llToStr(long long i)													// converts a long long (or smaller size integer-type value)to a string
{
	std::string tmpStr = "";
	
	do
	{
#if defined (WIN32) || defined (_WIN32_WCE)
		std::string tmp = "";
		tmp = (char)(i % 10 + '0');
		std::string tmp2 = tmpStr;
		tmpStr = tmp;
		tmpStr += tmp2;
#else
		tmpStr = (char)(i % 10 + '0') + tmpStr;
#endif
	}
	while ((i /= 10) > 0);
	
	return tmpStr;
}

std::string VSXML::strErr(int e)
{
	#ifndef ERR_BUF_SIZE
	#define ERR_BUF_SIZE 512
	#endif
	char errbuf[ERR_BUF_SIZE];
	
	#ifdef __STDC_WANT_SECURE_LIB__
	strerror_s(errbuf, ERR_BUF_SIZE, e);
	#else
	strncpy(errbuf, strerror(e), ERR_BUF_SIZE);
	#endif
	
	std::string ret = errbuf;
	
	return ret;
}

std::vector<VSXML*> VSXML::GetChildrenByAttribute(const char *szAttrib, const char *szValue, unsigned int uiMaxMatches)
{
  std::vector<VSXML *> ret;
  VSXML *n;
  for (unsigned int i = 0; i < this->Children.size(); i++)
  {
    n = this->Children[i];
    for (unsigned int j = 0; j < n->Attributes.size(); j++)
    {
      if (strcmp(n->Attributes[j].name.c_str(), szAttrib))
        continue;
      if ((szValue == NULL) || 
        (!n->Attributes[j].noValue && (strcmp(n->Attributes[j].value.c_str(), szValue) == 0)))
      {
        ret.push_back(n);
        if (uiMaxMatches && (uiMaxMatches <= ret.size()))
          break;
      }
      
    }
    if (uiMaxMatches && (uiMaxMatches <= ret.size()))
      return ret;
		if (n->ChildCount())
		{
			std::vector<VSXML *> childMatches = n->GetChildrenByAttribute(szAttrib, szValue);
			size_t stTop = childMatches.size();
			if (uiMaxMatches)
			{
			  stTop = uiMaxMatches - ret.size();
			  if (stTop > childMatches.size())
			    stTop = childMatches.size();
			}
		  for (size_t k = 0; k < stTop; k++)
			  ret.push_back(childMatches[k]);
		}
  }
  return ret;
}

VSXML *VSXML::DeepestChild()
{
  size_t stDepth = 0;
  VSXML *start = this;
  VSXML *ret = this->DeepestChild(&start, &stDepth);
  return ret;
}

VSXML *VSXML::DeepestChild(VSXML **Node, size_t *stDepth)
{
  size_t d = this->NodeDepth();
  if (d > (*stDepth))
  {
    *stDepth = d;
    *Node = this;
  }
  for (unsigned int i = 0; i < this->Children.size(); i++)
    this->Children[i]->DeepestChild(Node, stDepth);
  return *Node;
}


size_t VSXML::NodeDepth()
{
  if (this->parent)
    return (this->parent->NodeDepth() + 1);
  else
    return 0;
}

std::vector<VSXML *> VSXML::NodePath()
{
  std::vector<VSXML *>ret;
  this->InsertNodeIntoPath(&ret);
  return ret;
}

void VSXML::InsertNodeIntoPath(std::vector<VSXML *> *v)
{
  v->insert(v->begin(), this);
  if (this->parent)
    this->parent->InsertNodeIntoPath(v);
}

std::string VSXML::NodePathText(bool fIncludeTextNode, bool fIncludeAttribs)
{
  std::string ret =  "<" + this->name;
  if (fIncludeAttribs)
  {
    xmlAttrib *a;
    for (unsigned int i = 0; i < this->Attributes.size(); i++)
    {
      a = &(this->Attributes[i]);
      ret += " " + a->name;
      if (!a->noValue)
        ret += "=\"" + this->Enquote(a->value) + "\"";
    }
  }
  ret += ">";
  if (fIncludeTextNode)
    ret += this->Trim(this->Text());
  if (this->parent)
    return (this->parent->NodePathText() + ret);
  else
    return ret;
}

VSXML *VSXML::MatchNodeByPath(VSXML *NodeToMatch, VSXML *LastMatch, bool fFindFirstOnly, bool fCaseSensitive,
    MatchType mtMatchText, int intMatchTextDepth, MatchType mtMatchAttribs, int intMatchAttribsDepth)
{
  /*
   * finds a match for a given VSXML node by path (and optionally by text and attributes)
   * inputs:
   */

  if (LastMatch == NULL)
  {
    this->mvLastSearchMatches.clear();  // clears match cache
    this->FindMatchingChildren(NodeToMatch, &(this->mvLastSearchMatches), fFindFirstOnly, fCaseSensitive,
          mtMatchText, intMatchTextDepth, mtMatchAttribs, intMatchAttribsDepth, NULL);
    if (this->mvLastSearchMatches.size())
      return this->mvLastSearchMatches[0];
    else
      return NULL;
  }
  else
  {
    bool fFoundLast = false;
    for (unsigned int i = 0; i < this->mvLastSearchMatches.size(); i++)
    {
      if (fFoundLast)
      {
        return this->mvLastSearchMatches[i];
      }
      if (this->mvLastSearchMatches[i] == LastMatch)
        fFoundLast = true;
    }
    return NULL;
  }
}

void VSXML::FindMatchingChildren(VSXML *NodeToMatch, std::vector<VSXML*> *vMatches, bool fStopAfterOne, 
    bool fCaseSensitive, MatchType mtMatchText, long long llMatchTextDepth, MatchType mtMatchAttribs, 
    long long llMatchAttribsDepth, size_t *stMatchDepth)
{
  VSXML *n;
  size_t d;
  bool fDelMatchDepth = false;
  if (stMatchDepth == NULL)
  {
    stMatchDepth = new size_t;
    fDelMatchDepth = true;
    *stMatchDepth = (NodeToMatch->ChildCount()) ? NodeToMatch->DeepestChild()->NodeDepth() : NodeToMatch->NodeDepth();
  }

  for (unsigned int i = 0; i < this->Children.size(); i++)
  {
    n = this->Children[i];
    d = n->NodeDepth();
    if (d < (*stMatchDepth))
    {
      n->FindMatchingChildren(NodeToMatch, vMatches, fStopAfterOne, fCaseSensitive, 
        mtMatchText, llMatchTextDepth, mtMatchAttribs, llMatchAttribsDepth, stMatchDepth);
    }
    else if (d == (*stMatchDepth))
    {
      if (n->Matches(NodeToMatch, fCaseSensitive, mtMatchText, llMatchTextDepth, mtMatchAttribs, llMatchAttribsDepth))
      {
        vMatches->push_back(n);
        if (fStopAfterOne)
        {
          if (fDelMatchDepth)
            delete stMatchDepth;
          return;
        }
      }
    }
  }
  if (fDelMatchDepth)
    delete stMatchDepth;
}

bool VSXML::Matches(VSXML *node, bool fCaseSensitive, 
  MatchType mtMatchText, long long llMatchTextDepth, MatchType mtMatchAttribs, long long llMatchAttribsDepth)
{
  std::vector<VSXML *> vMyPath = this->NodePath();
  std::vector<VSXML *> vComparePath = (node->ChildCount()) ? node->DeepestChild()->NodePath() : node->NodePath();
  if (vMyPath.size() != vComparePath.size())
    return false;
  
  int (*cmpfunc)(const char *, const char *);
  if (fCaseSensitive) 
    cmpfunc = strcmp;
  else
    cmpfunc = vsxml_istrcmp;
  
  VSXML *n1, *n2;
  if (llMatchTextDepth <= 0)
    llMatchTextDepth = vComparePath.size() + llMatchTextDepth;
  if (llMatchAttribsDepth <= 0)
    llMatchAttribsDepth = vComparePath.size() + llMatchAttribsDepth;

  bool fTextMatch = false;
  bool fAttribMatch = false;
  if (mtMatchAttribs == None)
    fAttribMatch = true;
  if (mtMatchText == None)
    fTextMatch = true;
  for (size_t i = 0; i < vMyPath.size(); i++)
  {
    n1 = vMyPath[i];
    n2 = vComparePath[i];
    // path matching
    if (cmpfunc(n1->name.c_str(), n2->name.c_str()))
      return false;
    // text matching
    if (mtMatchText != None)
    {
      if (i >= (size_t)llMatchTextDepth)
        continue;
      switch (mtMatchText)
      {
        case SubsetPlus:
        case SupersetPlus:
        case Exact:   
        case ExactPlus: // all
        {
          size_t l1 = n1->Text().length();
          size_t l2 = n2->Text().length();
          if ((l1 == 0) && (l2 == 0))
          {
            if ((mtMatchText == Exact) || (mtMatchText == ExactPlus))   // blank nodes count
              fTextMatch = true;
            break;
          }
          if (!this->TextMatches(n1, n2, mtMatchText, fCaseSensitive))
            return false;
          fTextMatch = true;
          break;
        }
        default:    // 1+, blanks don't count
        {
          if (fTextMatch)  // already have a partial match
            continue;
          fTextMatch = this->TextMatches(n1, n2, mtMatchText, fCaseSensitive);
        }
      }
    }
    // attrib matching
    if (mtMatchAttribs != None)
    {
      if (i >= (size_t)llMatchAttribsDepth)
        continue;
      switch (mtMatchAttribs)
      {
        case Superset:
        case SupersetPlus:
        case Exact:   
        case ExactPlus:   // all
        {
          if (!this->AttribsMatch(n1, n2, mtMatchAttribs, fCaseSensitive))
            return false;
          fAttribMatch = true;
          break;
        }
        default:  // 1+
        {
          if (fAttribMatch)
            continue;
          fAttribMatch = this->AttribsMatch(n1, n2, mtMatchAttribs, fCaseSensitive);
        }
      }
    }
  }
  return fAttribMatch && fTextMatch;
}

bool VSXML::TextMatches(VSXML *master, VSXML *compare, MatchType mtMatch, bool fCaseSensitive)
{
  switch (mtMatch)
  {
    case None:
    {
      return true;
    }
    case Subset:
    case SubsetPlus:        // substrings
    {
      if (fCaseSensitive)
      {
        if (strstr(master->Text().c_str(), compare->Text().c_str()))
            return true;
      }
      else
      { // we need to make copies & lowercase
        std::string strMaster = master->Text().c_str();
        if (strMaster.length() == 0)
          return false;
        std::string strCompare = compare->Text();
        if (strCompare.length() == 0)
          return false;
        std::transform(strMaster.begin(), strMaster.end(), strMaster.begin(), tolower);
        std::transform(strCompare.begin(), strCompare.end(), strCompare.begin(), tolower);
        if (strstr(strMaster.c_str(), strCompare.c_str()))
          return true;
      }
      break;
    }
    default:                // full string
    {
      int (*cmpfunc)(const char *, const char *);
      if (fCaseSensitive)
        cmpfunc = strcmp;
      else
        cmpfunc = vsxml_istrcmp;
      std::string strMaster = master->Text();
      std::string strCompare = compare->Text();
      if (cmpfunc(master->Text().c_str(), compare->Text().c_str()) == 0)
        return true;
    }
  }
  return false;
}

bool VSXML::AttribsMatch(VSXML *master, VSXML *compare, MatchType mtMatch, bool fCaseSensitive)
{
  int (*cmpfunc)(const char *, const char *);
  if (fCaseSensitive)
    cmpfunc = strcmp;
  else
    cmpfunc = vsxml_istrcmp;
  switch (mtMatch)
  {
    case None:
    {
      return true;
    }
    case Subset:
    case SubsetPlus:
    {
      std::vector<xmlAttrib> *m = &(master->Attributes);
      std::vector<xmlAttrib> *c = &(compare->Attributes);
      for (unsigned int i = 0; i < m->size(); i++)
      {
        for (unsigned int j = 0; j < c->size(); i++)
        {
          if (cmpfunc(m->at(i).name.c_str(), c->at(i).name.c_str()) == 0)
            if ((mtMatch == Subset) ||
                (cmpfunc(m->at(i).value.c_str(), c->at(j).value.c_str()) == 0))
              return true;
        }
      }
      break;
    }
    case Superset:          // |
    case SupersetPlus:      // } all master nodes must be in compare (compare is then a superset of master)
    case Exact:             // | 
    case ExactPlus:         // } all master nodes in compare & no extras
    {
      std::vector<xmlAttrib> *m = &(master->Attributes);
      std::vector<xmlAttrib> *c = &(compare->Attributes);
      if (((mtMatch == Exact) || (mtMatch == ExactPlus)) &&
          c->size() != m->size())
        return false;   // trivial case: attribute counts don't match
      bool bFound;
      for (unsigned int i = 0; i < m->size(); i++)
      {
        bFound = false;
        for (unsigned int j = 0; j < c->size(); j++)
        {
          if (cmpfunc(m->at(i).name.c_str(), c->at(j).name.c_str()) == 0)
          {
            if ((mtMatch == Superset) || (mtMatch == Exact) ||
                (cmpfunc(m->at(i).value.c_str(), c->at(j).value.c_str()) == 0))
            {
              bFound = true;
              break;
            }
          }
        }
        if (((mtMatch == Exact) || (mtMatch == ExactPlus)) && (!bFound))
          return false;
      }
    }
  }
  return true;
}

std::vector<VSXML *> VSXML::FindNodesByXPath(std::string strXPath, bool fCaseSensitive)
{
  std::vector<VSXML *>ret;
  if (this->parent && (strXPath.find("/") != 0))
    return ret;   // not a relative path, and we aren't the ultimate ancestor
 
  std::vector<VSXML *> vLeaves;
  std::vector<std::string> vXPath = this->Split(&strXPath, "/");
  this->GetLeaves(&vLeaves);
  for (size_t i = 0; i < vLeaves.size(); i++)
  {
    std::vector<VSXML *> vLeafPath = vLeaves[i]->NodePath();
    for (size_t j = 0; j < vLeafPath.size(); j++)
    {
      bool fSkip = false;
      for (size_t k = 0; k < ret.size(); k++)
      {
        if (ret[k] == vLeafPath[j])
        {
          fSkip = true;
          break;
        }
      }
      if (!fSkip)
        if (vLeafPath[j]->MatchesXPath(strXPath, fCaseSensitive))
        {
          ret.push_back(vLeafPath[j]);
        }
    }
  }
  return ret;
}

void VSXML::GetLeaves(std::vector<VSXML*> *v)
{
  for (size_t i = 0; i < this->Children.size(); i++)
  {
    if (this->Children[i]->ChildCount())
      this->Children[i]->GetLeaves(v);
    else
      v->push_back(this->Children[i]);
  }
}

bool VSXML::MatchesXPath(std::string strXPath, bool fCaseSensitive)
{
  std::vector<std::string> vPaths = this->Split(&strXPath, "|");
  if (vPaths.size() > 1)
  {
    for (size_t i = 0; i < vPaths.size(); i++)
    {
      std::string strPath = this->Trim(vPaths[i]);
      if (this->MatchesXPath(strPath))
        return true;
    }
    return false;
  }
  // trivial cases
  if (strXPath.length() == 0)
    return false;
  if ((strcmp(strXPath.c_str(), "/") == 0) && (this->parent == NULL))
    return true;
  
  if (strXPath[0] == '/') 
  { 
    //if (this->parent)
    //  if ((strXPath.length() == 1) || (strXPath[1] != '/'))
    //    return false;
    strXPath = strXPath.substr(1);
  }
  
  VSXML *compare = this;
  std::vector<std::string> vXPath = this->Split(&strXPath, "/");
  int (*cmpfunc)(const char*, const char*);
  if (fCaseSensitive)
    cmpfunc = strcmp;
  else
    cmpfunc = vsxml_istrcmp;
  
  size_t stXPathLen = vXPath.size();
  if (stXPathLen)
  {
    if (cmpfunc(vXPath[stXPathLen-1].c_str(), "text()") == 0)
      vXPath.erase(vXPath.begin() + stXPathLen - 1);    // text node specifier: handled higher up
    else if ((vXPath[stXPathLen-1].length()) && (vXPath[stXPathLen-1][0] == '@'))
      vXPath.erase(vXPath.begin() + stXPathLen - 1);    // attribute specifier: handled higher up
  }
  while(vXPath.size())    // resolve leading .. to parents to get starting node
  {
    if (strcmp(vXPath[0].c_str(), "..") == 0)
    {
      if (compare->parent)
        compare = compare->parent;
      else
        return false;
    }
    else
      break;
    vXPath.erase(vXPath.begin());
  }
  
  std::vector<VSXML *> vNodePath = compare->NodePath();
  if (vNodePath.size() < vXPath.size())   // x-path may be shorter than node path, but not the other way around
    return false;
  size_t stXPathIDX = 0;
  bool fMatchAny = false;
  VSXML *n1, *n2;
  bool fMatchedOR = false;
  size_t i;
  for (i = 0; i < vNodePath.size(); i++)
  {
    if (stXPathIDX >= vXPath.size())
      return false;
    if (strlen(vXPath[stXPathIDX].c_str()) == 0)    // empty path element matches any element
    {
      stXPathIDX++;
      fMatchAny = true;
      continue;
    }
    std::vector<std::string> vMatchParts = this->Split(&(vXPath[stXPathIDX]), "[");
    n1 = vNodePath[i];
    std::vector<std::string> vMatchNameAndValue = this->Split(&(vMatchParts[0]), "=");
    if (cmpfunc(n1->name.c_str(), vMatchNameAndValue[0].c_str()))
    {
      if (fMatchAny || (strcmp(vMatchNameAndValue[0].c_str(), "*") == 0))
        continue;
      else
        return false;
    }
    else if (vMatchNameAndValue.size() > 1)
    {
      if (cmpfunc(this->Trim(n1->Text()).c_str(), vMatchNameAndValue[1].c_str()))
      {
        if (fMatchAny)
          continue;
        else
          return false;
      }
    }
    
    size_t pos = vXPath[stXPathIDX].find("[");
    if (pos != std::string::npos)
    {
      std::string strMatch = this->Trim(vXPath[stXPathIDX].substr(pos), "[]");
      std::vector<std::string> vChildMatches = this->Split(&strMatch, "][");
      for (size_t childNodeIDX = 0; childNodeIDX < vChildMatches.size(); childNodeIDX++)
      {
        if (childNodeIDX)
        {
          i++;
          if (i >= vNodePath.size())
            return false;
          n1 = vNodePath[i];
        }
        std::string strMatch2 = vChildMatches[childNodeIDX];
        if (strMatch2.find("last()") != std::string::npos)
        {
          unsigned int uiLast = (unsigned int)(n1->ChildCount());
          CHAR sz[32];
          sprintf(sz, "%u", uiLast);
          strMatch2 = strReplace(strMatch2, "last()", sz);
        }
        std::vector<std::string> vIndeces = this->Split(&strMatch2, "-");
        if (((vIndeces.size() == 1) && (this->isInteger(vIndeces[0].c_str()))) ||
            ((vIndeces.size() == 2) && (this->isInteger(vIndeces[1].c_str())) && (this->isInteger(vIndeces[1].c_str()))))
        {
          int intMatchIndex1 = atoi(vIndeces[0].c_str());
          int intMatchIndex2 = intMatchIndex1;
          if (vIndeces.size() > 1)
            intMatchIndex2 = atoi(vIndeces[1].c_str());
          if (intMatchIndex1 > intMatchIndex2)
          {
            int swap = intMatchIndex1;
            intMatchIndex1 = intMatchIndex2;
            intMatchIndex2 = swap;
          }

          if (intMatchIndex1 < 1)    // w3c defines starting index as 1
            return false;
          if (n1->parent == NULL)
            if (intMatchIndex1 > 1)   // remember match index 2 is at least = match index 1
              return false;           // no siblings to position relative to
          
          int intNodeIndex = -1;
          int intMatchedNodes = 0;
          for (size_t c = 0; c < n1->parent->Children.size(); c++)
          {
            n2 = vNodePath[i]->parent->Children[c];
            if (cmpfunc(n2->name.c_str(), n1->name.c_str()))
              continue;
            intMatchedNodes++;
            if (n2 == n1)
            {
              intNodeIndex = intMatchedNodes;
              break;
            }
          }
          if ((intNodeIndex < intMatchIndex1) || (intNodeIndex > intMatchIndex2))
            return false;
          fMatchedOR = true;
          fMatchAny = false;
          stXPathIDX++;
          continue;
        }
        std::vector<std::string> vSubORMatches = this->Split(&strMatch2, " or ");
        fMatchedOR = false;
        for (size_t i_or = 0; i_or < vSubORMatches.size(); i_or++)
        {
          size_t stMatchedANDS = 0;
          std::vector<std::string> vSubANDMatches = this->Split(&(vSubORMatches[i_or]), " and ");
          for (size_t i_and = 0; i_and < vSubANDMatches.size(); i_and++)
          {
            std::string strMatch3 = this->Trim(vSubANDMatches[i_and]);
            std::vector<std::string> vSubMatchParts = this->SplitMatch(&strMatch3);
            strMatch3 = this->Trim(vSubMatchParts[0]);
            if (strMatch3.length() && (strMatch3[0] == '@'))  // match an attribute on this node
            {
              strMatch3 = strMatch3.substr(1);
              if (this->Attributes.size() == 0)
                break;

              xmlAttrib *a;
              for (size_t j = 0; j < n1->Attributes.size(); j++)
              {
                a = &(n1->Attributes[j]);
                if (strcmp(strMatch3.c_str(), "*") && cmpfunc(a->name.c_str(), strMatch3.c_str()))
                    continue;
                if (vSubMatchParts.size() > 2)
                {
                  if (a->noValue)
                    break;
                  if (this->OperatorMatch(a->value.c_str(), vSubMatchParts[1].c_str(), vSubMatchParts[2].c_str(), fCaseSensitive))
                    stMatchedANDS++;
                }
                else if (a->noValue)
                  stMatchedANDS++;
              }
            }
            else    // match the text on a child node
            {
              if (this->ChildCount() == 0)
                break;
              if (vSubMatchParts.size() < 3)
                break;
              for (size_t c = 0; c < n1->Children.size(); c++)
              {
                if (this->OperatorMatch(n1->Children[c]->Text().c_str(), 
                  vSubMatchParts[1].c_str(), vSubMatchParts[2].c_str(), fCaseSensitive))
                {
                  stMatchedANDS++;
                  break;
                } 
              }
            }
          }
          if (stMatchedANDS == vSubANDMatches.size())
          {
            fMatchedOR = true;
            break;
          }
        }
        if (!fMatchedOR)
          return false;
      }
    }
    fMatchAny = false;
    stXPathIDX++;
  }
  return ((stXPathIDX == vXPath.size()) && (i = vNodePath.size())) ? true : false;
}

bool VSXML::isInteger(const CHAR *sz)
{
  if (strlen(sz) == 0)
    return false;
  size_t stStart = 0;
  if (sz[0] == '-')
    stStart = 1;
  for (size_t i = stStart; i < strlen(sz); i++)
  {
    if (strchr("0123456789", sz[i]) == NULL)
      return false;
  }
  return true;
}

bool VSXML::isNumeric(const CHAR *sz)
{
  if (strlen(sz) == 0)
    return false;
  bool bHaveDecimal = false;
  size_t stStart = 0;
  if (sz[0] == '-')
    stStart = 1;
  for (size_t i = stStart; i < strlen(sz); i++)
  {
    if (sz[1] == '.')
    {
      if (bHaveDecimal)
        return false;
      else
      {
        bHaveDecimal = true;
        continue;
      }
    }
    if (strchr("0123456789", sz[i]) == NULL)
      return false;
  }
  return true;
}

bool VSXML::OperatorMatch(const CHAR *szMaster, const CHAR *szOperator, const CHAR *szCompare, bool fCaseSensitive)
{
  int (*cmpfunc)(const char *, const char *);
  #ifdef WIN32
  const char *(*posfunc)(const char *, const char *);
  #else
  char *(*posfunc)(const char *, const char *);
  #endif
  
  if (fCaseSensitive)
  {
    cmpfunc = strcmp;
    posfunc = strstr;
  }
  else
  {
    cmpfunc = vsxml_istrcmp;
    posfunc = vsxml_stristr;
  }
  
  // string (and some numeric) comparisons
  double dblMaster = strtod(szMaster, NULL);
  double dblCompare = strtod(szCompare, NULL);
  bool fNumeric = this->isNumeric(szMaster);
  
  if (strcmp(szOperator, "!=") == 0)
  {
    if (fNumeric)
      return(dblMaster == dblCompare) ? false : true;
    else
      return (cmpfunc(szMaster, szCompare)) ? true : false;
  } 
  if (strcmp(szOperator, "=") == 0)
  {
    if (fNumeric)
      return (dblMaster == dblCompare) ? true : false;
    else
      return (cmpfunc(szMaster, szCompare)) ? false : true;
  }
  if (strcmp(szOperator, "~=") == 0)
    return (posfunc(szMaster, szCompare)) ? false : true;
  
  if (strcmp(szOperator, "=~") == 0)
    return (posfunc(szCompare, szMaster)) ? false : true;

  // numeric comparisons  
  
  if (strcmp(szOperator, "<") == 0)
    return (dblMaster < dblCompare) ? true : false;
  if (strcmp(szOperator, ">") == 0)
    return (dblMaster > dblCompare) ? true : false;
  
  // remaining operators are only >= and <=; so we can just check for equality
  return (dblMaster == dblCompare) ? true : false;
}

std::vector<std::string> VSXML::SplitMatch(std::string *str)
{
  // should return either of the following:
  //  - one-element array containing the match string
  //  - three-element array containing match name, match operator, match value
  const CHAR *sz = str->c_str();
  std::vector<std::string> ret;
  std::vector<std::string> operators;
  operators.push_back("!=");
  operators.push_back("<=");
  operators.push_back(">=");
  // my own operators (not part of the xpath spec)
  operators.push_back("~=");    // substring match
  operators.push_back("=~");    // superstring match
  // end my own operators
  operators.push_back("=");
  operators.push_back(">");
  operators.push_back("<");
  
  for (size_t i = 0; i < operators.size(); i++)
  {
    if (strstr(sz, operators[i].c_str()))
    {
      ret = this->Split(str, operators[i]);
      ret.insert(ret.begin() + 1, operators[i]);   // put the operator back in the mix
      if (ret.size() > 2)
        ret[2] = this->Trim(ret[2], "\"");
      break;
    }
  }
  
  if (ret.size() == 0)
    ret.push_back(*str);
  return ret;
}

