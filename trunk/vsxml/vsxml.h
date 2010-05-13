// Very Simple XML node class
#ifndef _VSXML_H_
#define _VSXML_H_

#include <vector>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string>
#include <stdlib.h>
#include <string.h>
#include <algorithm>        // for std::transform
#if defined(WIN32) || defined (_W32_WCE)
  #include "getline.h"
  #include <windows.h>			// required for dir enumeration stuff, but you should be referencing this anyways, to get WIN32 defined...
  #include <direct.h>

  // windows compat stuff
  #define ssize_t long
  // ok, I don't get the point of "supporting" POSIX funcs when you rename them and spew warnings. Typical M$
  #define getcwd(a, b) _getcwd(a, b)
  #define chdir(a) _chdir(a)
  #define NEWLINE "\r\n"
#else
  #include <unistd.h>
  #define NEWLINE "\n"
  #define CHAR char
#endif

#define FILE_WRITE_CHUNKSIZE    4096

//using namespace std;
struct xmlAttrib							// xml attribute
{
	std::string name;							// attrib name
	std::string value;							// attrib value
	bool noValue;							// flag if there is no inherint value for this attribute
};

struct xmlMetaTag							// meta tag struct
{	
	std::string name;							// name of tag
	std::vector<xmlAttrib> Attributes;			// list of attributes
};

struct QuoteTranslation						// struct of translation mechanisms
{
	std::string UnquotedVal;						// quote from this
	std::string QuotedVal;						// quote to this
};

enum MatchType              // search matching, beyond path
{
  None,
  Subset,
  SubsetPlus,
  Superset,
  SupersetPlus,
  Exact,
  ExactPlus
};

// When doing a match, a searching node is queried with a comparison node
// Matchtypes and what they mean relative to the comparison node
// MatchType          Attribute       Corresponding Value   TextNode
// None               -               -                     -
// Subset             1+              -                     1+ substring
// SubsetPlus         1+              match                 all substring, extra non-blanks ok
// Superset           all(extra ok)   -                     1+ exact
// SupersetPlus       all(extra ok)   match                 all exact, extra non-blanks are ok
// Exact              all             -                     all exact
// ExactPlus          all             match                 all exact

class VSXML									// defines an XML node
{
	public:
		VSXML();							// constructor
		//VSXML(const CHAR *szName);
		~VSXML();							// destructor
		bool CaseSensitive;					// is this object case sensitive?
		bool AllowCondensedNodes;			// allow condensed nodes when no text / children, eg: <node attrib="value"[ ...]/>
		bool IncludeXMLSpec;        // include the leading <?xml blahblahblah ?> -- default is yes, except for html (root node is HTML or html) -- only applies to the root element
		bool IsQNode;               // marks this as a node of type <?node attrib="value"?>
		bool SuppressComments;        // set true to disable all comments your app may add to the xml
	  bool Condensed;
	  bool WhiteSpaceMatters;
		bool NoisyDeath;
		bool AutoDeleteMe;
		std::vector<xmlAttrib> Attributes;		// atributes on this node
		std::vector<xmlMetaTag> MetaTags;		// meta tags on this node
		std::vector<VSXML *> Children;				// child nodes
		std::vector<std::string> TextNodes;			// all text nodes
		std::vector<std::string> ErrorLog;			// log of errors and info on this node
		std::string name;						// name of node
		
		bool ParseFile(std::string filename);	// load this node from a file
		bool ParseString(std::string *in);		// load this node from a string (pointer version)
		bool ParseString(std::string in);		// load this node from a string (value version)
		
		VSXML *AddChild(std::string strName = "");	// add a child node; return new child
		long long AddChild(VSXML *node);			// add a child node (existing node)
		bool DelChild(unsigned int idx);	// delete a child node by index
		bool DelChild(VSXML *node);			// delete a child node by value
		bool operator==(VSXML *compare);		// compare myself with another node
		
		bool HasAttrib(std::string name);		// check if attribute exists
		std::string GetAttrib(std::string name, std::string defaultval = "");	// get attribute value
		void SetAttrib(std::string name, std::string value);	// set attribute value
		void SetAttrib(std::string name, int value);  // as above, but for numeric value
		void SetAttrib(std::string name, double value, int precision);
		void SetNoValAttrib(std::string name);
		void DelAttrib(std::string name);
		
		std::string Text();						// gets consolidated text nodes of this node into a string
		void SetText(std::string str, bool blnQuote = true);			// sets text node on this node (overwrite)
		void AddText(std::string str, bool blnQuote = true);			// adds text node
		std::string RenderToString(unsigned int indent = 0); // renders self and all children to a string
		bool RenderToFile(std::string filename, bool overwrite = false);	// renders self and all children to a file
    void AddComment(std::string strComment);
    	
		bool HasChild(std::string NodeName);		// do we have a child by that name?
		size_t ChildCount(std::string NodeName = "");	// how many children by that name do we have?
		VSXML* GetChildByName(std::string NodeName, unsigned int idx = 0);	// get a child by name and index of that name
		std::vector<VSXML*> GetChildrenByAttribute(const char *szAttrib, const char *szValue = NULL, unsigned int uiMaxMatches = 0);  // get all children with matching attribute (eg, id, class, etc); NULL value returns all children with attribute match, irrespective of value
		std::string GetTextChildContents(std::string NodeName, std::string defaultVal = "", unsigned int idx = 0); // get the contents of just one child
		
		void Clear();						// clear out all data
		std::string LastError();					// get last error / log
		void ClearErrors();					// clear out all errors
		void SetWhiteSpaceMatters() {this->Condensed = false; this->WhiteSpaceMatters = true;};
		VSXML *MatchNodeByPath(VSXML *NodeToMatch, VSXML *LastMatch = NULL, bool fFindFirstOnly = false, 
      bool fCaseSensitive = true, 
      MatchType mtMatchText = None,       int intMatchTextDepth = 0,
      MatchType mtMatchAttributes = None, int intMatchAttribsDepth = 0);
		size_t NodeDepth();   // returns the depth of this node
		std::string NodePathText(bool fIncludeTextNode = false, bool fIncludeAttribs = false);     // returns the condensed path leading up to, and including, this node
		std::vector<VSXML *>NodePath();
		bool Matches(VSXML *node, bool fCaseSensitive = true, 
		  MatchType mtMatchText = None,    long long llMatchTextDepth = 0, 
      MatchType mtMatchAttribs = None, long long llMatchAttribsDepth = 0);
    VSXML *DeepestChild();    // returns the first child found of deepest depth in the tree -- useful for path matching purposes
		std::string Enquote(std::string str);					// XML quote a string
		std::string Dequote(std::string str);					// XML De-quote a string
		
		std::vector<VSXML *> FindNodesByXPath(std::string strXPath, bool fCaseSensitive = true);
		void GetLeaves(std::vector<VSXML *> *v);    // returns all leaf nodes -- this can be used to traverse all linear paths from the root
		bool MatchesXPath(std::string strXPath, bool fCaseSensitive = true);
		std::vector<std::string> Split(std::string *str, std::string delimiter, 
			bool dontBreakQuotes = true, bool trimElements = false);	// split a std::string into parts, based on a delimiter
		std::string Trim(std::string str, std::string trimchars = " \n\r\t");	// simple string trimming (whitespace)
    bool isInteger(const CHAR *sz);
    bool isNumeric(const CHAR *sz);
  protected:
		bool IsComment;             // rendered as a comment; name doesn't matter: text is represented within the comments
		void FindMatchingChildren(VSXML *NodeToMatch, std::vector<VSXML *> *vMatches, bool fStopAfterOne = false, 
        bool fCaseSensitive = true,
        MatchType mtMatchText = None,    long long llMatchTextDepth = 0, 
        MatchType mtMatchAttribs = None, long long llMatchAttribsDepth = 0,
        size_t *stMatchDepth = NULL);
		void InsertNodeIntoPath(std::vector<VSXML *> *v);
	  void Init();
		static std::vector<QuoteTranslation> QuoteTranslations;	// used to translate between regular strings and quoted xml strings
		VSXML *self;						// used for comparison ==
		VSXML *parent;						// my parent, if there is one
	  std::vector<VSXML *> mvLastSearchMatches;
		void Log(std::string str);				// log an error
		int GetAttribIDX(std::string name);		// get the index of an attribute, by name
		size_t StringCount(std::string *haystack, std::string needle,
			size_t start = 0, size_t end = 0);	// count occurrences of string needle in string haystack
		std::string GetFirstTagName(std::string *content);	// get the first tagname in a string
		size_t GetCloseTagPos(std::string *content, size_t start, 
			std::string tag);							// find where tag closes
		void CompressNewlines(std::string *in);			// remove extraneous newlines (you don't see them in xml anyway)
		std::string strReplace(std::string haystack, std::string needle, std::string newneedle);	// replace string needle with string newneedle in string haystack
		void InitQuoteTranslations();				// load up quote translations
		std::string llToStr(long long i);				// converts a long long (or smaller var size) to string
		std::string strErr(int e);
    VSXML *DeepestChild(VSXML **Node, size_t *stDepth);
    bool TextMatches(VSXML *master, VSXML *compare, MatchType mtMatch, bool fCaseSensitive);
    bool AttribsMatch(VSXML *master, VSXML *compare, MatchType mtMatch, bool fCaseSensitive);
    std::vector<std::string> SplitMatch(std::string *str);
    bool OperatorMatch(const CHAR *szMaster, const CHAR *szOperator, const CHAR *szCompare, bool fCaseSensitive);
};

#ifdef WIN32
const char *stristr(const char *sz1, const char *sz2);
#else
char *stristr(const char *sz1, const char *sz2);
#endif

#endif
