#include "server.h"
#include "Authenticate.h"
#include "MimeType.h"
#include "DavProps.h"
#include "directory_listing.h"
#include "FileDetailsPage.h"
#include "FileProperties.h"
#include "ChrootHelper.h"
#include "Events.h"
#include "ID3.h"
#include "upload.h"
#include "proxy.h"
#include "fnmatch.h"
#include "AccessTokens.h"
#include "VPath.h"

const char *HTTPMethods[]={"HEAD","GET","POST","PUT","DELETE","MKCOL","PROPFIND","PROPPATCH","MOVE","COPY","OPTIONS","CONNECT","LOCK","UNLOCK",NULL};

const char *HeaderStrings[]={"Authorization","Proxy-Authorization","Host","Destination","Content-Type","Content-Length","Depth","Overwrite","User-Agent","Cookie","If-Modified-Since","Accept-Encoding","Icy-MetaData","Referer","Connection","Upgrade","Sec-WebSocket-Key","Sec-Websocket-Key1", "Sec-Websocket-Key2","Sec-WebSocket-Protocol","Sec-WebSocket-Version","Origin", NULL};

typedef enum {HEAD_AUTH, HEAD_PROXYAUTH, HEAD_HOST, HEAD_DEST, HEAD_CONTENT_TYPE, HEAD_CONTENT_LENGTH, HEAD_DEPTH, HEAD_OVERWRITE, HEAD_AGENT, HEAD_COOKIE, HEAD_IFMOD_SINCE, HEAD_ACCEPT_ENCODING, HEAD_ICECAST,HEAD_REFERER, HEAD_CONNECTION, HEAD_UPGRADE, HEAD_WEBSOCK_KEY, HEAD_WEBSOCK_KEY1, HEAD_WEBSOCK_KEY2, HEAD_WEBSOCK_PROTOCOL, HEAD_WEBSOCK_VERSION, HEAD_ORIGIN} THeaders;


int HTTPServerReadBody(HTTPSession *Session, char **Data)
{
char *Tempstr=NULL;
int bytes_read=0, len;

if (Session->ContentSize > 0)
{
	*Data=SetStrLen(*Data,Session->ContentSize+10);
  while (bytes_read < Session->ContentSize)
  {
  len=STREAMReadBytes(Session->S, (*Data) + bytes_read, Session->ContentSize-bytes_read);
  if (len < 1) break;
  bytes_read+=len;
  }
}
else
{
  Tempstr=STREAMReadLine(Tempstr,Session->S);
  while (Tempstr)
  {
		len=StrLen(Tempstr);
    *Data=CatStrLen(*Data,Tempstr,len);
		bytes_read+=len;
    Tempstr=STREAMReadLine(Tempstr,Session->S);
  }
}

DestroyString(Tempstr);
return(bytes_read);
}


int HTTPServerDecideToCompress(HTTPSession *Session, char *Path)
{
//If client hasn't asked for it (Accept-Encoding) then don't
if (! Session) return(FALSE);
if (! (Session->Flags & SESSION_ENCODE_GZIP)) return(FALSE);

if (IsProxyMethod(Session->MethodID)) return(FALSE);
if (Settings.Flags & FLAG_COMPRESS) return(TRUE);
if ((Settings.Flags & FLAG_PARTIAL_COMPRESS) && (! Path)) return(TRUE);

return(FALSE);
}




void HTTPServerHandleAuthHeader(HTTPSession *Heads,int HeaderType, char *Type, char *Data)
{
char *Tempstr=NULL, *Name=NULL, *Value=NULL, *ptr;
char *nonce=NULL, *cnonce=NULL, *request_count=NULL, *qop=NULL, *algo=NULL, *uri=NULL;
int len;

if (strcmp(Type,"Basic")==0)
{
	Tempstr=DecodeBase64(Tempstr, &len, Data);
	ptr=GetToken(Tempstr,":",&Heads->UserName,0);
	Heads->Password=CopyStr(Heads->Password,ptr);
}
else if (strcmp(Type,"Digest")==0)
{
	uri=CopyStr(uri,"");
	algo=CopyStr(algo,"");
	ptr=GetNameValuePair(Data,",","=",&Name,&Value);
	while (ptr)
	{
		if (StrLen(Name) && StrLen(Value))
		{
		StripLeadingWhitespace(Name);
		StripLeadingWhitespace(Value);
		if (strcmp(Name,"username")==0) Heads->UserName=CopyStr(Heads->UserName,Value);
		if (strcmp(Name,"response")==0) Heads->Password=CopyStr(Heads->Password,Value);
		if (strcmp(Name,"nonce")==0) nonce=CopyStr(nonce,Value);
		if (strcmp(Name,"cnonce")==0) cnonce=CopyStr(cnonce,Value);
		if (strcmp(Name,"nc")==0) request_count=CopyStr(request_count,Value);
		if (strcmp(Name,"qop")==0) qop=CopyStr(qop,Value);
		if (strcmp(Name,"uri")==0) uri=CopyStr(uri,Value);
		if (strcmp(Name,"algorithm")==0) algo=CopyStr(algo,Value);
		}
		
	ptr=GetNameValuePair(ptr,",","=",&Name,&Value);
	}

// server nonce (nonce), request counter (nc), client nonce (cnonce), quality of protection code (qop) and HA2 result is calculated. The result is the "response" value provided by the client.

if (StrLen(qop)) Heads->AuthDetails=MCopyStr(Heads->AuthDetails,uri,":",algo,":",nonce,":",request_count,":",cnonce,":",qop, NULL);
else Heads->AuthDetails=CopyStr(Heads->AuthDetails,nonce);

}

DestroyString(qop);
DestroyString(uri);
DestroyString(algo);
DestroyString(Name);
DestroyString(Value);
DestroyString(nonce);
DestroyString(cnonce);
DestroyString(Tempstr);
DestroyString(request_count);
}


void HTTPServerParsePostContentType(HTTPSession *Session, char *Data)
{
char *ptr, *Name=NULL, *Value=NULL;

ptr=GetToken(Data,";",&Session->ContentType,0);
if (ptr)
{
while (isspace(*ptr)) ptr++;

ptr=GetNameValuePair(ptr,";","=",&Name,&Value);
while (ptr)
{
	if (strcmp(Name,"boundary")==0) Session->ContentBoundary=CopyStr(Session->ContentBoundary,Value);
	ptr=GetNameValuePair(ptr,";","=",&Name,&Value);
}
}

DestroyString(Name);
DestroyString(Value);
}


//Will only return false if FLAG_SSL_CERT_REQUIRED is set
int HTTPServerCheckCertificate(HTTPSession *Session, STREAM *S)
{
char *ptr;
int result=TRUE;

	ptr=STREAMGetValue(S,"SSL-Certificate-Verify");

	if (StrLen(ptr) && (strcmp(ptr,"no certificate") !=0)  )
	{
		LogToFile(Settings.LogPath,"AUTH SSL Certificate Provided by '%s@%s'. Subject=%s Issuer=%s",Session->UserName,Session->ClientIP,STREAMGetValue(S,"SSL-Certificate-Subject"), STREAMGetValue(S,"SSL-Certificate-Issuer"));

		if (strcmp(ptr,"OK")!=0)
		{
			if (Settings.AuthFlags & FLAG_AUTH_CERT_REQUIRED)
			{
				LogToFile(Settings.LogPath,"AUTH: ERROR: SSL Certificate REQUIRED from client '%s@%s'. Invalid Certificate. Error was: %s",Session->UserName,Session->ClientIP, ptr);
				result=FALSE;
			}
			else LogToFile(Settings.LogPath,"AUTH: SSL Certificate Optional for client '%s@%s'. Invalid Certificate. Error was: %s",Session->UserName,Session->ClientIP, ptr);

			LogFileFlushAll(TRUE);
		}
	}
	else if (Settings.AuthFlags & FLAG_AUTH_CERT_REQUIRED) LogToFile(Settings.LogPath,"AUTH: ERROR: SSL Certificate REQUIRED from client '%s@%s'. Missing Certificate.",Session->UserName,Session->ClientIP);


return(result);
}



//This function reads the first line of an HTTP Request, including the Method, URL, and cgi arguments
void HTTPServerParseCommand(HTTPSession *Session, STREAM *S, char *Command)
{
char *Token=NULL, *ptr, *tmp_ptr;
int val;

GetSockDetails(S->in_fd,&Session->ServerName,&Session->ServerPort,&Session->ClientIP,&val);
GetHostARP(Session->ClientIP, &Token, &Session->ClientMAC);
if ((Settings.Flags & FLAG_LOOKUP_CLIENT) && StrLen(Session->ClientIP)) Session->ClientHost=CopyStr(Session->ClientHost,IPStrToHostName(Session->ClientIP));

LogToFile(Settings.LogPath,"");
//Log first line of the response

Token=MCopyStr(Token, "NEW REQUEST: ",Session->ClientHost," (",Session->ClientIP,") ", Command, NULL);
if (Settings.Flags & FLAG_SSL)
{
	Session->Cipher=CopyStr(Session->Cipher,STREAMGetValue(S,"SSL-Cipher"));
	Token=MCatStr(Token,"  SSL-CIPHER=", Session->Cipher, NULL);
	if (! HTTPServerCheckCertificate(Session,S)) exit(1);

	//Set the Username to be the common name signed in the certificate. If it doesn't
	//authenticate against a user then we can query for a username later
	Session->UserName=CopyStr(Session->UserName,STREAMGetValue(Session->S,"SSL-Certificate-CommonName"));
	if (Settings.AuthFlags & FLAG_AUTH_CERT_SUFFICIENT)
	{
		if (StrLen(Session->UserName)) Session->AuthFlags |= FLAG_AUTH_PRESENT;
	}
}

LogToFile(Settings.LogPath, "%s", Token);

//Read Method (GET, POST, etc)
ptr=GetToken(Command,"\\S",&Session->Method,0);
Session->MethodID=MatchTokenFromList(Session->Method,HTTPMethods,0);

//Read URL
ptr=GetToken(ptr,"\\S",&Token,0);

//Read Protocol (HTTP1.0, HTTP1.1, etc)
ptr=GetToken(ptr,"\\S",&Session->Protocol,0);
if (! StrLen(Session->Protocol)) Session->Protocol=CopyStr(Session->Protocol,"HTTP/1.0");

tmp_ptr=Token;

//Clip out arguments from URL
tmp_ptr=strchr(Token,'?');
if (tmp_ptr)
{
	*tmp_ptr='\0';
	tmp_ptr++;
//	Session->Arguments=HTTPUnQuote(Session->Arguments,tmp_ptr);

	//Don't unquote arguments here, one of them might contain '&'
	Session->Arguments=CopyStr(Session->Arguments,tmp_ptr);
}


//URL with arguments removed is the 'true' URL
Session->OriginalURL=CopyStr(Session->OriginalURL,Token);
if (StrLen(Session->OriginalURL)==0) Session->OriginalURL=CopyStr(Session->OriginalURL,"/");

if 
(
	(strncasecmp(Session->OriginalURL,"http:",5)==0) ||
	(strncasecmp(Session->OriginalURL,"https:",6)==0)
)
{
	if (Session->MethodID==METHOD_GET) 
	{
		Session->Method=CopyStr(Session->Method,"RGET");
		Session->MethodID=METHOD_RGET;
	}

	if (Session->MethodID==METHOD_POST)
	{
		Session->Method=CopyStr(Session->Method,"RPOST");
		Session->MethodID=METHOD_RPOST;
	}
}

DestroyString(Token);
}


int HTTPServerReadHeaders(HTTPSession *Session)
{
char *Tempstr=NULL, *Token=NULL, *ptr;
ListNode *Curr;
int val;

HTTPSessionClear(Session);
Tempstr=STREAMReadLine(Tempstr,Session->S);
if (! Tempstr) return(FALSE);

StripTrailingWhitespace(Tempstr);

//First line of the HTTP request is the 'Command' in the form "<method> <url>?<arguments> <HTTP version>"
HTTPServerParseCommand(Session, Session->S, Tempstr);


Tempstr=STREAMReadLine(Tempstr,Session->S);

if (Tempstr)
{
	StripTrailingWhitespace(Tempstr);
	StripLeadingWhitespace(Tempstr);
}

while (StrLen(Tempstr) )
{

	if (Settings.Flags & FLAG_LOG_VERBOSE) LogToFile(Settings.LogPath,"<< %s",Tempstr);
	ptr=GetToken(Tempstr,":",&Token,0);

	while (isspace(*ptr)) ptr++;
	val=MatchTokenFromList(Token,HeaderStrings,0);
	ListAddNamedItem(Session->Headers,Token,CopyStr(NULL,ptr));

	switch (val)
	{
	case HEAD_PROXYAUTH:
			if (IsProxyMethod(Session->MethodID))
			{
			ptr=GetToken(ptr,"\\S",&Token,0);
			HTTPServerHandleAuthHeader(Session,val,Token,ptr);
			Session->AuthFlags |= FLAG_AUTH_PRESENT;
			}
	break;

	case HEAD_AUTH:
			if (IsProxyMethod(Session->MethodID))
			{
				Session->RemoteAuthenticate=CopyStr(Session->RemoteAuthenticate,ptr);
			}

			if (StrLen(Session->UserName)==0)
			{
				ptr=GetToken(ptr,"\\S",&Token,0);
				HTTPServerHandleAuthHeader(Session,val,Token,ptr);
				Session->AuthFlags |= FLAG_AUTH_PRESENT;
			}
	break;

	case HEAD_HOST:
		Session->Host=CopyStr(Session->Host,ptr);
		ptr=strchr(Session->Host,':');
		if (! ptr) 
		{
			Token=FormatStr(Token,":%d",Settings.Port);
			Session->Host=CatStr(Session->Host,Token);
		}
	break;

	case HEAD_DEST:
		Session->Destination=HTTPUnQuote(Session->Destination,ptr);
	break;

	case HEAD_CONTENT_TYPE:
		HTTPServerParsePostContentType(Session, ptr);
		break;

	case HEAD_CONTENT_LENGTH:
		Session->ContentSize=atoi(ptr);
	break;

	case HEAD_DEPTH:
		if (strcasecmp(ptr,"infinity")==0) Session->Depth=INT_MAX;
		else Session->Depth=atoi(ptr);
	break;

	case HEAD_OVERWRITE:
		if (*ptr=='T') Session->Flags |= SESSION_OVERWRITE;
	break;

	case HEAD_CONNECTION:
		if ((Settings.Flags & FLAG_KEEPALIVES) && (strcasecmp(ptr,"Keep-Alive")==0)) Session->Flags |= SESSION_KEEPALIVE;
	break;

	case HEAD_AGENT:
		Session->UserAgent=CopyStr(Session->UserAgent,ptr);
		Curr=ListGetNext(Settings.UserAgents);
		while (Curr)
		{
		if (fnmatch(Curr->Tag,Session->UserAgent,0)==0) 
		{
			if (Settings.Flags & FLAG_LOG_VERBOSE) LogToFile(Settings.LogPath,"Applying User Agent Settings: %s",Curr->Item);
			ParseConfigItemList((char *) Curr->Item);
		}
		Curr=ListGetNext(Curr);
		}
	break;

	case HEAD_COOKIE:
			if (StrLen(Session->Cookies)) Session->Cookies=MCopyStr(Session->Cookies,"; ",ptr,NULL);
			else Session->Cookies=CopyStr(Session->Cookies,ptr);
			Session->AuthFlags |= FLAG_AUTH_PRESENT;
	break;

	case HEAD_REFERER:
		Session->ClientReferrer=CopyStr(Session->ClientReferrer,ptr);
	break;

	case HEAD_ACCEPT_ENCODING:
		ptr=GetToken(ptr,",",&Token,0);
		while (ptr)
		{
			if (strcmp(Token,"gzip")==0) Session->Flags|=SESSION_ENCODE_GZIP;
			if (strcmp(Token,"x-gzip")==0) Session->Flags|=SESSION_ENCODE_GZIP | SESSION_ENCODE_XGZIP;
		ptr=GetToken(ptr,",",&Token,0);
		}
	break;

	case HEAD_ICECAST:
		if (atoi(ptr)) Session->Flags |= SESSION_ICECAST;
	break;

	case HEAD_IFMOD_SINCE:
		Session->IfModifiedSince=DateStrToSecs("%a, %d %b %Y %H:%M:%S %Z",ptr,NULL);
	break;

	case HEAD_UPGRADE:
		if ((strcasecmp(ptr,"Upgrade")==0) && SSLAvailable())
		{
			if (! HTTPServerActivateSSL(Session,Settings.SSLKeys)) return;
		} 
		else if (strcasecmp(ptr,"websocket")==0) Session->MethodID = METHOD_WEBSOCKET;
	break;

	case HEAD_WEBSOCK_KEY:
		Session->ContentBoundary=CopyStr(Session->ContentBoundary, ptr);
	break;

	case HEAD_WEBSOCK_KEY1:
		Session->ContentBoundary=CopyStr(Session->ContentBoundary, ptr);
		if (Session->MethodID==METHOD_WEBSOCKET) Session->MethodID = METHOD_WEBSOCKET75;
	break;

	case HEAD_WEBSOCK_KEY2:
		Session->ContentType=CopyStr(Session->ContentType, ptr);
		if (Session->MethodID==METHOD_WEBSOCKET) Session->MethodID = METHOD_WEBSOCKET75;
	break;

	case HEAD_WEBSOCK_PROTOCOL:
		Session->ContentType=CopyStr(Session->ContentType, ptr);
	break;

	case HEAD_WEBSOCK_VERSION:
	break;

	case HEAD_ORIGIN:
	break;
	}

Tempstr=STREAMReadLine(Tempstr,Session->S);
StripTrailingWhitespace(Tempstr);
StripLeadingWhitespace(Tempstr);
}


if (strstr(Session->Arguments,"AccessToken")) Session->AuthFlags |= FLAG_AUTH_PRESENT | FLAG_AUTH_ACCESS_TOKEN;


Session->URL=HTTPUnQuote(Session->URL,Session->OriginalURL);

if (*Session->URL=='/') Session->Path=CopyStr(Session->Path,Session->URL);
else Session->Path=MCopyStr(Session->Path,"/",Session->URL,NULL);

DestroyString(Tempstr);
DestroyString(Token);

return(TRUE);
}




void HTTPServerSendHeader(STREAM *S, char *Header, char *Value)
{
char *Tempstr=NULL;

Tempstr=MCopyStr(Tempstr,Header,": ",Value,"\r\n",NULL);
STREAMWriteLine(Tempstr,S);
if (Settings.Flags & FLAG_LOG_VERBOSE) LogToFile(Settings.LogPath,">> %s",Tempstr);
DestroyString(Tempstr);
}


void HTTPServerSendHeaders(STREAM *S, HTTPSession *Session, int Flags)
{
char *Tempstr=NULL;
ListNode *Curr;

Tempstr=MCopyStr(Tempstr,Session->Protocol," ",Session->ResponseCode,"\r\n",NULL);
STREAMWriteLine(Tempstr,S);
if (Settings.Flags & FLAG_LOG_VERBOSE) LogToFile(Settings.LogPath,">> %s",Tempstr);

HTTPServerSendHeader(S,"Date",GetDateStr("%a, %d %b %Y %H:%M:%S %Z",NULL));

if (Session->LastModified > 0) HTTPServerSendHeader(S,"Last-Modified",GetDateStrFromSecs("%a, %d %b %Y %H:%M:%S %Z",Session->LastModified,NULL));

if (Flags & HEADERS_AUTH) 
{
	if (Settings.AuthFlags & FLAG_AUTH_DIGEST) Tempstr=FormatStr(Tempstr,"Digest realm=\"%s\", qop=\"auth\", nonce=\"%x\"", Settings.AuthRealm, rand());
	else Tempstr=MCopyStr(Tempstr,"Basic realm=\"",Settings.AuthRealm,"\"",NULL);

	if (IsProxyMethod(Session->MethodID) ) HTTPServerSendHeader(S,"Proxy-Authenticate",Tempstr);
	else
	{
		Tempstr=MCopyStr(Tempstr,"Basic realm=\"",Settings.AuthRealm,"\"",NULL);
		HTTPServerSendHeader(S,"WWW-Authenticate",Tempstr);

		if (Settings.AuthFlags & FLAG_AUTH_DIGEST)
		{
			Tempstr=FormatStr(Tempstr,"Digest realm=\"%s\", qop=\"auth\", nonce=\"%x\"", Settings.AuthRealm, rand());
			HTTPServerSendHeader(S,"WWW-Authenticate",Tempstr);
		}
	}
}



//Special headers passed in for this transaction
Curr=ListGetNext(Session->Headers);
while (Curr)
{
	HTTPServerSendHeader(S, Curr->Tag, (char *) Curr->Item);
	Curr=ListGetNext(Curr);
}


//Custom headers defined in the config file
Curr=ListGetNext(Settings.CustomHeaders);
while (Curr)
{
	HTTPServerSendHeader(S, Curr->Tag, (char *) Curr->Item);
	Curr=ListGetNext(Curr);
}




if (Session->MethodID==METHOD_WEBSOCKET)
{
	HTTPServerSendHeader(S, "Upgrade", "WebSocket");
	HTTPServerSendHeader(S, "Connection", "Upgrade");
}
else 
{
	if ((Flags & HEADERS_USECACHE) && (Settings.DocumentCacheTime > 0))
	{
	Tempstr=FormatStr(Tempstr,"max-age=%d",Session->CacheTime);
	HTTPServerSendHeader(S, "Cache-Control", Tempstr);
	HTTPServerSendHeader(S,"Expires",GetDateStrFromSecs("%a, %d %b %Y %H:%M:%S %Z",time(NULL) + Session->CacheTime,NULL));
	}
	else 
	{
	HTTPServerSendHeader(S, "Cache-Control", "no-cache");
	HTTPServerSendHeader(S, "Pragma", "no-cache");
	}

	//Offer Upgrade to SSL if we have it
	if ((! Session->Flags & HTTP_SSL) &&  SSLAvailable()) 
	{
	HTTPServerSendHeader(S, "Upgrade", "TLS/1.0");
	}

	if ((Session->Flags & SESSION_KEEPALIVE) && (Flags & HEADERS_KEEPALIVE))
	{
		HTTPServerSendHeader(S, "Connection", "Keep-Alive");
		Session->Flags |= SESSION_REUSE;
	}
	else
	{
		HTTPServerSendHeader(S, "Connection", "close");
		Session->Flags &= ~SESSION_REUSE;
	}

	if ((Settings.AuthFlags & FLAG_AUTH_COOKIE) && (Session->Flags & SESSION_AUTHENTICATED) && (! (Session->AuthFlags & FLAG_AUTH_HASCOOKIE)))
	{
		if (StrLen(Session->UserName)) 
		{
			Tempstr=MakeAccessCookie(Tempstr, Session);
			HTTPServerSendHeader(S, "Set-Cookie", Tempstr);
		}
	}

//If we are running a CGI script, then the script will handle all headers relating to content
if (! (Flags & HEADERS_CGI)) 
{
	HTTPServerSendHeader(S, "DAV", "1");
	if (StrLen(Session->ContentType)) HTTPServerSendHeader(S,"Content-Type",Session->ContentType);
	else HTTPServerSendHeader(S,"Content-Type","octet/stream");


	if ((Session->Flags & SESSION_REUSE) || (Session->ContentSize > 0))
	{
		Tempstr=FormatStr(Tempstr,"%d",Session->ContentSize);
		HTTPServerSendHeader(S,"Content-Length",Tempstr);
	}
	
	//some clients use 'x-gzip' rather than just 'gzip'
	if (Session->Flags & SESSION_ENCODE_XGZIP) HTTPServerSendHeader(S,"Content-Encoding","x-gzip");
	else if (Session->Flags & SESSION_ENCODE_GZIP) HTTPServerSendHeader(S,"Content-Encoding", "gzip");
	
	
	//Blank line to end headers
	STREAMWriteLine("\r\n",S);
}
}

LogFileFlushAll(TRUE);
DestroyString(Tempstr);
}


void HTTPServerSendResponse(STREAM *S, HTTPSession *Session, char *ResponseLine, char *ContentType, char *Body)
{
HTTPSession *Response;
char *Tempstr=NULL;
long ResponseCode=0;

LogToFile(Settings.LogPath,"RESPONSE: '%s' to %s@%s for '%s %s'",ResponseLine,Session->UserName,Session->ClientIP,Session->Method,Session->Path);

ResponseCode=strtol(ResponseLine,NULL,10);

//Create 'Response' rather than using session, because things set by the client in 'Session' might
//get copied into the response and interfere with the response otherwise
Response=HTTPSessionCreate();

/*Copy Values from Session object into Response */
if (Session)
{
	Response->MethodID=Session->MethodID;
	Response->LastModified=Session->LastModified;
	Response->Flags |= Session->Flags & (SESSION_KEEPALIVE | SESSION_AUTHENTICATED);
	//Response->Flags |= SESSION_KEEPALIVE;
	Response->ClientIP=CopyStr(Response->ClientIP,Session->ClientIP);
	Response->Path=CopyStr(Response->Path,Session->Path);
	Response->Method=CopyStr(Response->Method,Session->Method);
	Response->URL=CopyStr(Response->URL,Session->URL);
	Response->UserName=CopyStr(Response->UserName,Session->UserName);
}

Response->ResponseCode=CopyStr(Response->ResponseCode,ResponseLine);
if (ResponseCode==302) SetVar(Response->Headers,"Location",Body);
else Response->ContentSize=StrLen(Body);
Response->ContentType=CopyStr(Response->ContentType,ContentType);


if (HTTPServerDecideToCompress(Session,NULL))
{
  Response->Flags |= SESSION_ENCODE_GZIP;
	Tempstr=SetStrLen(Tempstr,Response->ContentSize *2); 
  Response->ContentSize=CompressBytes(&Tempstr, "gzip",Body, StrLen(Body), 5);
}
else Tempstr=CopyStr(Tempstr,Body);


if ((ResponseCode==401) || (ResponseCode==407)) HTTPServerSendHeaders(S, Response,HEADERS_AUTH);
else HTTPServerSendHeaders(S, Response, HEADERS_KEEPALIVE);

STREAMWriteBytes(S,Tempstr,Response->ContentSize);
STREAMFlush(S);

/* If HTTPServerSendHeaders set SESSION_REUSE then set that in the Session object */
//if (Response->Flags & SESSION_REUSE) Session->Flags |= SESSION_REUSE;
//else Session->Flags &= ~SESSION_REUSE;


ProcessSessionEventTriggers(Response);
HTTPSessionDestroy(Response);

DestroyString(Tempstr);
}


void HTTPServerSendHTML(STREAM *S, HTTPSession *Session, char *Title, char *Body)
{
char *Tempstr=NULL;


Tempstr=FormatStr(Tempstr,"<html><body><h1>%s</h1>%s</body></html>",Title,Body);
HTTPServerSendResponse(S, Session, Title, "text/html",Tempstr);

DestroyString(Tempstr);
}



void SendICYMessage(STREAM *Output, const char *ICYMessage)
{
uint8_t len;
char *Tempstr=NULL;

	len=StrLen(ICYMessage);
	if (len > 0) len=(len / 16) + 1;
	Tempstr=SetStrLen(Tempstr,len * 16);
	memset(Tempstr,0,len * 16);
	strcpy(Tempstr,ICYMessage);
	STREAMWriteBytes(Output,(char *) &len,1);
	STREAMWriteBytes(Output,Tempstr,len * 16);

DestroyString(Tempstr);
}





HTTPSession *FileSendCreateSession(char *Path, HTTPSession *Request, ListNode *Vars, int ICYInterval)
{
	HTTPSession *Session;
	char *Tempstr=NULL;

	Session=HTTPSessionResponse(Request);
	Session->ResponseCode=CopyStr(Session->ResponseCode,"200 OK");
	Session->ContentType=CopyStr(Session->ContentType,GetVar(Vars,"ContentType"));
	Session->LastModified=atoi(GetVar(Vars,"MTime-secs"));
	Session->ContentSize=atoi(GetVar(Vars,"FileSize"));

	if (Session->Flags & SESSION_ICECAST) 
	{
		Session->Flags |= SESSION_ICECAST;
		Session->Protocol=CopyStr(Session->Protocol,"ICY");
		Tempstr=FormatStr(Tempstr,"%d",ICYInterval);
		SetVar(Session->Headers,"icy-metaint",Tempstr);
	}
	else if (HTTPServerDecideToCompress(Request,Path))
	{
		Session->ContentSize=0;
		Session->Flags |= SESSION_ENCODE_GZIP;
	}

	DestroyString(Tempstr);
	return(Session);
}



void IcecastSendData(STREAM *Input, STREAM *Output, int ICYInterval)
{
char *ICYMessage=NULL, *Tempstr=NULL;
int BuffSize=4096, BytesRead=0, len, result;
ListNode *Vars;

	Vars=ListCreate();
	SetVar(Vars, "Title",GetBasename(Input->Path));
	MediaReadDetails(Input,Vars);

	ICYMessage=SubstituteVarsInString(ICYMessage,"StreamTitle='$(Title)'; StreamArtist='$(Artist)';",Vars,0);
	LogToFile(Settings.LogPath,"Open: %s, message: %s",Input->Path,ICYMessage);
	Tempstr=SetStrLen(Tempstr,BuffSize);
	while (TRUE) 
	{
		len=ICYInterval-BytesRead;
		if (len > BuffSize) len=BuffSize;
		result=STREAMReadBytes(Input,Tempstr,len);
		if (result==EOF) break;

		BytesRead+=result;
		STREAMWriteBytes(Output,Tempstr,result);

		if (BytesRead==ICYInterval)
		{
			LogToFile(Settings.LogPath,"SEND ICY: %s",ICYMessage);
			BytesRead=0;
			SendICYMessage(Output, ICYMessage);
			//Don't send it again.
			ICYMessage=CopyStr(ICYMessage,"");
		}	
	}

ListDestroy(Vars,DestroyString);
DestroyString(ICYMessage);
DestroyString(Tempstr);
}


void HTTPServerHandleStream(STREAM *Output, HTTPSession *Session, char *SearchPath, int SendData)
{
char *Tempstr=NULL;
HTTPSession *Response;
ListNode *Vars;
STREAM *S;
glob_t Glob;
int i;


Vars=ListCreate();
SetVar(Vars,"ContentType","audio/mpeg");
Response=FileSendCreateSession("", Session, Vars, 0);
HTTPServerSendHeaders(Output, Response, FALSE);
STREAMFlush(Output);

Tempstr=MCopyStr(Tempstr,SearchPath,"/*",NULL);
glob(Tempstr,0,0,&Glob);

LogToFile(Settings.LogPath,"Stream from Dir: %s, %d files",SearchPath,Glob.gl_pathc);

for (i=0; i < Glob.gl_pathc; i++)
{
	S=STREAMOpenFile(Glob.gl_pathv[i],SF_RDONLY);
	if (S)
	{
		IcecastSendData(S, Output, 4096000);
		STREAMClose(S);
	}
}

globfree(&Glob);
DestroyString(Tempstr);
ListDestroy(Vars,DestroyString);
}



void HTTPServerFormatExtraHeaders(HTTPSession *Session, ListNode *Vars)
{
ListNode *Curr;
char *Tempstr=NULL;

Curr=ListGetNext(Vars);
while (Curr)
{
	if (strncmp(Curr->Tag,"Media-",6)==0)
	{
		Tempstr=MCopyStr(Tempstr,"X-",Curr->Tag,NULL);
		SetVar(Session->Headers,Tempstr,Curr->Item);
	}
Curr=ListGetNext(Curr);
}

DestroyString(Tempstr);
}


void HTTPServerSendFile(STREAM *S, HTTPSession *Session, char *Path, ListNode *Vars, int Flags)
{
STREAM *Doc;
HTTPSession *Response;
char *Buffer=NULL, *Tempstr=NULL;
int ICYInterval=4096000;

	Doc=STREAMOpenFile(Path, SF_RDONLY);
	if (! Doc) HTTPServerSendHTML(S, Session, "403 Forbidden","You don't have permission for that.");
	else
	{
		if (Session)
		{
			LogToFile(Settings.LogPath,"%s@%s (%s) downloading %s (%s bytes)",Session->UserName,Session->ClientHost,Session->ClientIP,Path,GetVar(Vars,"FileSize"));
		}

		Response=FileSendCreateSession(Path, Session, Vars, ICYInterval);
		MediaReadDetails(Doc,Vars);
		HTTPServerFormatExtraHeaders(Response,Vars);
		
		HTTPServerSendHeaders(S, Response, Flags);

		if (Response->Flags & SESSION_ENCODE_GZIP) 
		{
			STREAMAddStandardDataProcessor(S,"compression","gzip","CompressionLevel=1");
		}

		
		if (Flags & HEADERS_SENDFILE)
		{
		if (Session->Flags & SESSION_ICECAST) IcecastSendData(Doc, S, ICYInterval);
		else STREAMSendFile(Doc, S, 0, SENDFILE_KERNEL | SENDFILE_LOOP);
		}


	/* If HTTPServerSendHeaders set SESSION_REUSE then set that in the Session object 
	if (Response->Flags & SESSION_REUSE) Session->Flags |= SESSION_REUSE;
	else Session->Flags &= ~SESSION_REUSE;
	*/

		STREAMClose(Doc);
		HTTPSessionDestroy(Response);
	}

DestroyString(Buffer);
DestroyString(Tempstr);
}





void HTTPServerSendDocument(STREAM *S, HTTPSession *Session, char *Path, int Flags)
{
int result;
ListNode *Vars;

	Vars=ListCreate();

	if (StrLen(Path)==0) result=FILE_NOSUCH;
	else result=LoadFileRealProperties(Path, TRUE, Vars);

	if (result==FILE_NOSUCH) HTTPServerSendHTML(S, Session, "404 Not Found","Couldn't find that document.");
	else
	{
		//Set 'LastModified' so we can use it if the server sends 'If-Modified-Since'
	  Session->LastModified=atoi(GetVar(Vars,"MTime-secs"));

		//If we are asking for details of a file then we treat that as a directory function
		if ((result==FILE_DIR) || (strstr(Session->Arguments,"format=")))
		{
			HTTPServerSendDirectory(S,Session,Path,Vars);
		}
		else HTTPServerSendFile(S, Session, Path, Vars, Flags);
	}

ListDestroy(Vars,DestroyString);
}



int HTTPServerExecCGI(STREAM *ClientCon, HTTPSession *Session, const char *ScriptPath)
{
char *Tempstr=NULL;
int i;

      if (StrLen(Session->Group) && (! SwitchGroup(Session->Group)))
      {
        LogToFile(Settings.LogPath,"WARN: Failed to switch to group '%s' to execute script: %s",Session->Group, ScriptPath);
      }


    //Switch user. ALAYA WILL NOT RUN SCRIPTS AS ROOT!
      if ((geteuid()==0) && (! SwitchUser(Session->RealUser)))
      {
        LogToFile(Settings.LogPath,"ERROR: Failed to switch to user '%s' to execute script: %s",Session->RealUser, ScriptPath);
				return(FALSE);
      }

      if (geteuid()==0)
      {
        HTTPServerSendHTML(ClientCon, NULL, "403 Forbidden","Alaya will not run .cgi programs as 'root'.<br>\r\nTry setting 'Default User' in config file or command line.");
        LogToFile(Settings.LogPath, "Failed to switch user to '%s' for running a .cgi program. Will not run programs as 'root'. Set 'DefaultUser' in config file or command line.",Session->RealUser);
      }
      else
      {
        Session->ResponseCode=CopyStr(Session->ResponseCode,"200 OK");
        HTTPServerSendHeaders(ClientCon, Session, HEADERS_CGI);
        STREAMFlush(ClientCon);

        SetupEnvironment(Session, ScriptPath);
        Tempstr=FindScriptHandlerForScript(Tempstr,ScriptPath);
        if (Tempstr) LogToFile(Settings.LogPath,"Execute script: %s using handler '%s'",ScriptPath,Tempstr);
        else LogToFile(Settings.LogPath,"Execute script: %s QUERY_STRING= '%s'",ScriptPath,getenv("QUERY_STRING"));

        //Only do this late! Otherwise logging won't work.
        for (i=3; i < 1000; i++) close(i);

        if (StrLen(Tempstr)) execl(Tempstr, Tempstr, ScriptPath,NULL);
        else execl(ScriptPath,ScriptPath,NULL);

        /*If this code gets executed, then 'execl' failed*/
        HTTPServerSendHTML(ClientCon, Session, "403 Forbidden","You don't have permission for that.");
  
        //Logging won't work after we've closed all the file descriptors!
        LogToFile(Settings.LogPath,"Cannot execute script: %s",ScriptPath);
    }

	//if we get there then, for whatever reason, our script didn't run
	DestroyString(Tempstr);
	return(FALSE);
}



void HTTPServerHandlePost(STREAM *S, HTTPSession *Session)
{
char *Tempstr=NULL;
int bytes_read=0, result;

	if (strcmp(Session->ContentType,"application/x-www-form-urlencoded")==0) HTTPServerReadBody(Session, &Session->Arguments);
	else if (strncmp(Session->ContentType,"multipart/",10)==0) UploadMultipartPost(S, Session);
	HTTPServerSendResponse(S, Session, "302", "", Session->URL);

DestroyString(Tempstr);
}



void HTTPServerRecieveURL(STREAM *S,HTTPSession *Heads)
{
STREAM *Doc;
struct stat FileStat;
char *Buffer=NULL, *Tempstr=NULL;
int BuffSize=4096;


Doc=STREAMOpenFile(Heads->Path, SF_CREAT | SF_TRUNC | SF_WRONLY);

if (! Doc) HTTPServerSendHTML(S, Heads, "403 Forbidden","Can't open document for write.");
else
{
	fchmod(Doc->in_fd,0660); 

	Buffer=SetStrLen(Buffer,BuffSize);
	STREAMSendFile(S,Doc,Heads->ContentSize, SENDFILE_KERNEL | SENDFILE_LOOP);
	STREAMClose(Doc);

	stat(Heads->Path,&FileStat);
	LogToFile(Settings.LogPath,"%s@%s (%s) uploaded %s (%d bytes)",Heads->UserName,Heads->ClientHost,Heads->ClientIP,Heads->Path,FileStat.st_size);
	HTTPServerSendHTML(S, Heads, "201 Created","");
}


DestroyString(Tempstr);
DestroyString(Buffer);
}



void HTTPServerMkDir(STREAM *S,HTTPSession *Heads)
{
int result;

result=mkdir(Heads->Path, 0770);
if (result==0) HTTPServerSendHTML(S, Heads, "201 Created","");
else switch (errno)
{

	case EEXIST:
	HTTPServerSendHTML(S, Heads, "405 Method Not Allowed (exists)","");
	break;

	case ENOENT:
	case ENOTDIR:
	HTTPServerSendHTML(S, Heads, "409 Conflict","");
	break;

	case ENOSPC:
	HTTPServerSendHTML(S, Heads, "507 Insufficient Storage","");
	break;

	default:
	HTTPServerSendHTML(S, Heads, "403 Forbidden","You don't have permission for that.");
	break;

}

}

int HTTPServerDeleteCollection(HTTPSession *Session,char *Path)
{
struct stat FileStat;
glob_t myGlob;
int result, i;
char *Tempstr=NULL, *ptr;


LogToFile(Settings.LogPath,"%s@%s (%s) DeleteCollection: %s",Session->UserName,Session->ClientHost,Session->ClientIP,Path);


Tempstr=MCopyStr(Tempstr,Path,"/*",NULL);
glob(Tempstr,0,0,&myGlob);
for (i=0; i < myGlob.gl_pathc; i++)
{
	if (Settings.Flags & FLAG_LOG_VERBOSE) LogToFile(Settings.LogPath,"%s@%s (%s) DeleteSubItem: %s",Session->UserName,Session->ClientHost,Session->ClientIP,myGlob.gl_pathv[i]);

	ptr=myGlob.gl_pathv[i];
	if ((strcmp(ptr,".") !=0) && (strcmp(ptr,"..") !=0)) 
	{
	stat(ptr,&FileStat);
	if (S_ISDIR(FileStat.st_mode)) HTTPServerDeleteCollection(Session,ptr);
	else unlink(ptr);
	}

}

DestroyString(Tempstr);
globfree(&myGlob);
result=rmdir(Path);

return(result);
}


void HTTPServerDelete(STREAM *S,HTTPSession *Heads)
{
int result;
struct stat FileStat;

stat(Heads->Path,&FileStat);
if (S_ISDIR(FileStat.st_mode)) result=HTTPServerDeleteCollection(Heads,Heads->Path);
else result=unlink(Heads->Path);

if (result==0) HTTPServerSendHTML(S, Heads, "200 Deleted","");
else switch (errno)
{

	case ENOENT:
	case ENOTDIR:
	HTTPServerSendHTML(S, Heads, "404 No such item","");
	break;

	case EISDIR:
	HTTPServerSendHTML(S, Heads, "409 Conflict","");
	break;

	default:
	HTTPServerSendHTML(S, Heads, "403 Forbidden","You don't have permission for that.");
	break;

}


}





void HTTPServerCopy(STREAM *S,HTTPSession *Heads)
{
int result=-1;
char *Tempstr=NULL, *Host=NULL, *Destination=NULL;

LogToFile(Settings.LogPath,"HTTP COPY: [%s] [%s]",Heads->URL,Heads->Destination);

result=CopyURL(Heads, Heads->URL, Heads->Destination);

switch (result)
{
	case 0:
		 HTTPServerSendHTML(S, Heads, "201 Created","");
	break;

	case ENOENT:
	case ENOTDIR:
	HTTPServerSendHTML(S, Heads, "404 No such item","");
	break;

	case EISDIR:
	HTTPServerSendHTML(S, Heads, "409 Conflict","");
	break;

	case EEXIST:
	HTTPServerSendHTML(S, Heads, "412 Precondition failed. File exists, but 'Overwrite' set to false","");
	break;

	default:
	HTTPServerSendHTML(S, Heads, "403 Forbidden","You don't have permission for that.");
	break;

}


DestroyString(Host);
DestroyString(Tempstr);
DestroyString(Destination);
}



void HTTPServerMove(STREAM *S,HTTPSession *Heads)
{
int result;
char *Tempstr=NULL, *Host=NULL, *Destination=NULL, *ptr;

Tempstr=CopyStr(Tempstr,Heads->Destination);
ptr=Tempstr;
if (strncmp(ptr,"http:",5)==0) ptr+=5;
if (strncmp(ptr,"https:",6)==0) ptr+=6;
while (*ptr=='/') ptr++;

ptr=GetToken(ptr,"/",&Host,0);

Destination=MCopyStr(Destination,Heads->StartDir,ptr,NULL);
result=rename(Heads->Path,Destination);



if (result==0) HTTPServerSendHTML(S, Heads, "201 Moved","");
else switch (errno)
{

	case ENOENT:
	case ENOTDIR:
	HTTPServerSendHTML(S, Heads, "404 No such item","");
	break;

	case EISDIR:
	HTTPServerSendHTML(S, Heads, "409 Conflict","");
	break;


	default:
	HTTPServerSendHTML(S, Heads, "403 Forbidden","You don't have permission for that.");
	break;

}


DestroyString(Host);
DestroyString(Tempstr);
DestroyString(Destination);
}

void HTTPServerHandleLock(STREAM *S, HTTPSession *ClientHeads)
{

if (ClientHeads->MethodID==METHOD_LOCK)
{
	if (access(ClientHeads->Path, F_OK) !=0)
	{
			//File does not exist. We must create it
	}

}
else
{
	
}

}



void HTTPServerOptions(STREAM *S,HTTPSession *ClientHeads)
{
char *Tempstr=NULL;

STREAMWriteLine("HTTP/1.1 200 OK\r\n",S);
Tempstr=CopyStr(Tempstr,GetDateStr("Date: %a, %d %b %Y %H:%M:%S %Z\r\n",NULL));
STREAMWriteLine(Tempstr,S);
STREAMWriteLine("Content-Length: 0\r\n",S);
STREAMWriteLine("Public: OPTIONS, TRACE, GET, HEAD, DELETE, PUT, POST, COPY, MOVE, MKCOL, PROPFIND, PROPPATCH, LOCK, UNLOCK, SEARCH\r\n",S);
STREAMWriteLine("Allow: OPTIONS, GET, HEAD, DELETE, PUT, POST, COPY, MOVE, MKCOL, PROPFIND, PROPPATCH\r\n",S);
STREAMWriteLine("DASL:\r\n",S);
STREAMWriteLine("DAV: 1\r\n",S);

DestroyString(Tempstr);
}







int HTTPServerSetUserContext(HTTPSession *Session)
{
char *ChrootDir=NULL, *Tempstr=NULL;

Session->StartDir=CopyStr(Session->StartDir,Settings.DefaultDir);
ChrootDir=CopyStr(ChrootDir,Settings.DefaultDir);

LogToFile(Settings.LogPath,"SETCTX: %d %s home=%s",Session->MethodID,ChrootDir,Session->HomeDir);

if (IsProxyMethod(Session->MethodID))
{
	//Do not chroot for proxy commands
}
else 
{
	if (Settings.Flags & FLAG_CHHOME) ChrootDir=CopyStr(ChrootDir,Session->HomeDir);

		//if (Settings.Flags & FLAG_LOG_VERBOSE) 
LogToFile(Settings.LogPath,"ChRoot to: %s home=%s",ChrootDir,Session->HomeDir);

	if (chdir(ChrootDir) !=0) 
	{
		LogToFile(Settings.LogPath,"ERROR: CHDIR FAILED: %d %s %s",getuid(),ChrootDir,strerror(errno));
		HTTPServerSendHTML(Session->S, Session, "500 Internal Server Error","Problem switching to home-directory");
		LogFileFlushAll(TRUE);
 		_exit(1);
	}
	chroot(".");
	Session->StartDir=CopyStr(Session->StartDir,"/");
}

/*
/Not working yet
else if (Settings.Flags & FLAG_CHSHARE) 
{
	chdir(Settings.DefaultDir);
	chroot(".");
	if (strncmp(Session->StartDir,Settings.DefaultDir,StrLen(Settings.DefaultDir))==0)
	{
		Tempstr=MCopyStr(Tempstr,"/",Session->StartDir+StrLen(Settings.DefaultDir),NULL);
		chdir(Tempstr);
		Session->StartDir=CopyStr(Session->StartDir,Tempstr);
	}
}
*/

Session->StartDir=SlashTerminateDirectoryPath(Session->StartDir);

LogToFile(Settings.LogPath,"User Context: Chroot: %s, StartDir: %s, HomeDir: %s, UserID: %d, GroupID: %d,",ChrootDir, Session->StartDir, Session->HomeDir, Session->RealUserUID,Session->GroupID);

if (Session->GroupID > 0)
{
	if (setgid(Session->GroupID) != 0)
	{
		HTTPServerSendHTML(Session->S, Session, "500 Internal Server Error","Problem switching to configured user-group");
	  LogToFile(Settings.LogPath,"ERROR: Failed to switch group to %s/%d. Exiting",Session->RealUser,Session->RealUserUID);
 		_exit(1);
	}
}
else if (Settings.DefaultGroupID > 0) 
{
	if (setgid(Settings.DefaultGroupID) != 0)
	{
		HTTPServerSendHTML(Session->S, Session, "500 Internal Server Error","Problem switching to configured user-group");
		LogToFile(Settings.LogPath,"ERROR: Failed to switch group to %s/%d. Exiting",Session->RealUser,Session->RealUserUID);
 		_exit(1);
	}
}

DropCapabilities(CAPS_LEVEL_CHROOTED);

if (setresuid(Session->RealUserUID,Session->RealUserUID,Session->RealUserUID)!=0)
{
	HTTPServerSendHTML(Session->S, Session, "500 Internal Server Error","Problem switching to configured user");
  LogToFile(Settings.LogPath,"ERROR: Failed to switch user to %s/%d. Exiting",Session->RealUser,Session->RealUserUID);
  _exit(1);
}

//drop everything! (In case someting went wrong with setresuid) 
DropCapabilities(CAPS_LEVEL_SESSION);

DestroyString(Tempstr);
DestroyString(ChrootDir);

return(TRUE);
}




int HTTPMethodAllowed(HTTPSession *Session) 
{
char *Token=NULL, *ptr;

if (StrLen(Settings.HttpMethods)==0) return(TRUE);

ptr=GetToken(Settings.HttpMethods,",",&Token,0);
while (ptr)
{
if (strcmp(Token,Session->Method)==0) 
{
DestroyString(Token);
return(TRUE);
}

ptr=GetToken(ptr,",",&Token,0);
}

DestroyString(Token);
return(FALSE);
}


int HTTPServerAuthenticate(HTTPSession *Session)
{
	int result=FALSE;

	//This handles someone clicking a 'logout' button
	if (! HTTPServerHandleRegister(Session, LOGIN_CHECK_ALLOWED))
	{
		LogToFile(Settings.LogPath,"AUTH: Forcing Relogin for  %s@%s (%s) %s %s",Session->ClientIP,Session->ClientHost,Session->ClientIP,Session->Method,Session->Path);
		return(FALSE);
	}


	if (Session->Flags & SESSION_AUTHENTICATED) 
	{
		if (strcmp(Session->UserName,Session->AuthenticatedUser)==0) 
		{
			LogToFile(Settings.LogPath,"AUTH: Session Keep-Alive active, reusing authentication for %s@%s (%s) %s %s",Session->ClientIP,Session->ClientHost,Session->ClientIP,Session->Method,Session->Path);
			return(TRUE);
		}
		else LogToFile(Settings.LogPath,"AUTH: ERROR: Session Keep-Alive active, but user has changed to %s@%s (%s) %s %s. Refusing authentication",Session->ClientIP,Session->ClientHost,Session->ClientIP,Session->Method,Session->Path);
	}



	//Consider AccessToken Authentication for this URL!
	if ((! (Session->Flags & SESSION_AUTHENTICATED)) && (Session->AuthFlags & FLAG_AUTH_ACCESS_TOKEN)) ParseAccessToken(Session);


	if (Session->AuthFlags & FLAG_AUTH_PRESENT)
	{
		//if this looks back-to-front it's because for some methods we only get the username
		//after we've completed authentication (e.g. it's taken from a cookie)

		//ANYTHING OTHER THAN TRUE FROM AUTHENTICATE MEANS IT FAILED
		if ((Authenticate(Session)==TRUE) && StrLen(Session->UserName)) result=TRUE;

		//If authentication provided any users settings, then apply those
		if (StrLen(Session->UserSettings)) ParseConfigItemList(Session->UserSettings);

		//The FLAG_SSL_CERT_REQUIRED flag might have been set by user settings
		//during authentication, so check it again here
		if (! HTTPServerCheckCertificate(Session, Session->S)) result=FALSE;

		if (result) HTTPServerHandleRegister(Session, LOGGED_IN);
		else HTTPServerHandleRegister(Session, LOGIN_FAIL);
	}

	if (result==TRUE) 
	{
		Session->AuthenticatedUser=CopyStr(Session->AuthenticatedUser,Session->UserName);
		Session->Flags |= SESSION_AUTHENTICATED;
	}

	return(result);
}



/************************************************************

This function reformats button presses from the interactive
directory listing. It reformats them into GET style URLs that
are easier to work with. This method also means that we can
trigger the same actions either with a button, or with an
anchor (href) tag.

*************************************************************/

int HTTPServerProcessActions(STREAM *S, HTTPSession *Session)
{
typedef enum {ACT_NONE, ACT_GET, ACT_DEL, ACT_DEL_SELECTED, ACT_RENAME, ACT_EDIT, ACT_MKDIR, ACT_PACK, ACT_SAVE_PROPS, ACT_EDIT_WITH_ACCESSTOKEN, ACT_M3U, ACT_UPLOAD} TServerActs;
char *QName=NULL, *QValue=NULL, *Name=NULL, *Value=NULL, *ptr;
char *Arg1=NULL, *Arg2=NULL, *FileProperties=NULL, *SelectedFiles=NULL;
TServerActs Action=ACT_NONE;
int result=FALSE;


	//QName and QValue will be HTTP quoted, so arguments must be 
	//dquoted after unpacking from the URL
	ptr=GetNameValuePair(Session->Arguments,"&","=",&QName,&QValue);
	while (ptr)
	{
		Name=HTTPUnQuote(Name,QName);
		Value=HTTPUnQuote(Value,QValue);
		QValue=CopyStr(QValue,"");

		switch (*Name)
		{
		case 'd':
		if (strncasecmp(Name,"del:",4)==0) 
		{
			Action=ACT_DEL;
			Arg1=CopyStr(Arg1,Name+4);
		}
		else if (strncasecmp(Name,"delete-selected:",16)==0) 
		{
			Action=ACT_DEL_SELECTED;
			Arg1=CopyStr(Arg1,Name+16);
		}
		break;

		case 'e':
		if (strncasecmp(Name,"edit:",5)==0)
		{
			Action=ACT_EDIT;
			Arg1=CopyStr(Arg1,Name+5);
		}
		break;

		case 'f':
		if (strncasecmp(Name,"fileproperty:",13)==0) FileProperties=MCatStr(FileProperties,"&",Name,"=",Value,NULL);
		break;

		case 'g':
		if (strncasecmp(Name,"get:",4)==0) 
		{
			Action=ACT_GET;
			Arg1=CopyStr(Arg1,Name+4);
		}
		else if (strncasecmp(Name,"genaccess:",10)==0)
		{
			Action=ACT_EDIT_WITH_ACCESSTOKEN;
			Arg1=CopyStr(Arg1,Name+10);
		}
		break;
		
		case 'm':
		if (strncasecmp(Name,"mkdir:",6)==0) 
		{
			Action=ACT_MKDIR;
			Arg1=CopyStr(Arg1,Name+6);
		}
		else if (strcasecmp(Name,"mkdir")==0) QValue=HTTPUnQuote(QValue,Value);
		else if (strncasecmp(Name,"m3u:",4)==0) 
		{
			Action=ACT_M3U;
			Arg1=CopyStr(Arg1,Name+4);
		}
		break;

		case 'r':	
		if (strncasecmp(Name,"renm:",5)==0) 
		{
			Action=ACT_RENAME;
			Arg1=CopyStr(Arg1,Name+5);
		}
		else if (strcasecmp(Name,"renameto")==0) QValue=HTTPUnQuote(QValue,Value);
		break;

		case 'p':
		if (strncasecmp(Name,"pack:",5)==0) 
		{
			Action=ACT_PACK;
			Arg1=CopyStr(Arg1,Name+5);
		}
		else if (strcasecmp(Name,"packtype")==0) QValue=HTTPUnQuote(QValue,Value);
		else if (strcasecmp(Name,"packtarget")==0) QValue=HTTPUnQuote(QValue,Value);
		break;

		case 's':
		if (strncasecmp(Name,"sprops:",7)==0) 
		{
			Action=ACT_SAVE_PROPS;
			Arg1=CopyStr(Arg1,Name+7);
		}
		else if (strcasecmp(Name,"selected")==0) QValue=HTTPUnQuote(QValue,Value);
		break;

		case 'u':
		if (strncasecmp(Name,"upload:",7)==0) 
		{
			Action=ACT_UPLOAD;
			Arg1=CopyStr(Arg1,Name+7);
		}
		break;
		}

		//these are secondary arguments in the query string, whereas all the above are the primary 
		//request that defines what action we're taking
		if (StrLen(QValue)) Arg2=MCatStr(Arg2, Name, "=", QValue, "&",NULL);

		ptr=GetNameValuePair(ptr,"&","=",&QName,&QValue);
	}


	//Most of these actions are handled in 'directory_listing.c' Many of them concern buttons on the 'edit' page for a file
	//Look in the top of 'directory_listing.c' for an enum that each 'format=' argument will map to
	switch (Action)
	{
		case ACT_NONE:
		break;

		case ACT_EDIT:
		result=TRUE;
		Value=MCopyStr(Value,Arg1,"?format=edit",NULL);
		Session->LastModified=0;
		HTTPServerSendResponse(S, Session, "302", "", Value);
		result=TRUE;
		break;

		case ACT_EDIT_WITH_ACCESSTOKEN:
		result=TRUE;
		Value=MCopyStr(Value,Arg1,"?format=editaccesstoken",NULL);
		Session->LastModified=0;
		HTTPServerSendResponse(S, Session, "302", "", Value);
		result=TRUE;
		break;

	  case ACT_DEL:
		result=TRUE;
		Value=MCopyStr(Value,Arg1,"?format=delete",NULL);
		Session->LastModified=0;
		HTTPServerSendResponse(S, Session, "302", "", Value);
		break;

		case ACT_DEL_SELECTED:
		result=TRUE;
		Value=MCopyStr(Value,Arg1,"?format=delete-selected&",Arg2,NULL);
		Session->LastModified=0;
		HTTPServerSendResponse(S, Session, "302", "", Value);
		break;

	  case ACT_RENAME: 
		if (StrLen(Arg2))
		{
			Value=MCopyStr(Value,Arg1,"?format=rename&",Arg2,NULL);
			Session->LastModified=0;
			HTTPServerSendResponse(S, Session, "302", "", Value);
			result=TRUE;
		}
		break;

	  case ACT_MKDIR: 
		if (StrLen(Arg2))
		{
			Value=MCopyStr(Value,Arg1,"?format=mkdir&",Arg2,NULL);
			Session->LastModified=0;
			HTTPServerSendResponse(S, Session, "302", "", Value);
			result=TRUE;
		}
		break;

		case ACT_M3U:
		Value=MCopyStr(Value,Arg1,"?format=m3u&",Arg2,NULL);
		HTTPServerSendResponse(S, Session, "302", "", Value);
		result=TRUE;
		break;

		case ACT_GET:
		HTTPServerSendResponse(S, Session, "302", "", Arg1);
		result=TRUE;
		break;

		case ACT_SAVE_PROPS:
		Value=MCopyStr(Value,Arg1,"?format=saveprops",FileProperties,NULL);
		Session->LastModified=0;
		HTTPServerSendResponse(S, Session, "302", "", Value);
		break;

		case ACT_PACK:
		Value=MCopyStr(Value,Arg1,"?format=pack&",Arg2,NULL);
		Session->LastModified=0;
		HTTPServerSendResponse(S, Session, "302", "", Value);
		break;

		case ACT_UPLOAD:
		Value=MCopyStr(Value,Arg1,"?format=upload",NULL);
		HTTPServerSendResponse(S, Session, "302", "", Value);
		result=TRUE;
		break;
	}

DestroyString(FileProperties);
DestroyString(SelectedFiles);
DestroyString(QName);
DestroyString(QValue);
DestroyString(Name);
DestroyString(Value);
DestroyString(Arg1);
DestroyString(Arg2);

return(result);
}





int HTTPServerValidateURL(HTTPSession *Session, char **Token)
{
char *ptr;

ptr=GetToken(Settings.ForbiddenURLStrings,",",Token,0);
while (ptr)
{
	if (strstr(Session->OriginalURL,*Token)) 
	{
		Session->Flags |= SESSION_ERR_BADURL;
		LogToFile(Settings.LogPath,"ERROR: INVALID URL: %s",Session->URL);
		return(FALSE);
	}
	ptr=GetToken(ptr,",",Token,0);
}

return(TRUE);
}


int HTTPServerActivateSSL(HTTPSession *Session,ListNode *Keys)
{
ListNode *Curr;
int Flags=0;

Curr=ListGetNext(Keys);
while (Curr)
{
STREAMSetValue(Session->S,Curr->Tag,(char *) Curr->Item);
Curr=ListGetNext(Curr);
}

Flags |= LU_SSL_PFS;
if (Settings.AuthFlags & (FLAG_AUTH_CERT_REQUIRED | FLAG_AUTH_CERT_SUFFICIENT | FLAG_AUTH_CERT_ASK)) Flags |= LU_SSL_VERIFY_PEER;

if (DoSSLServerNegotiation(Session->S,Flags))
{
	Session->Flags |= HTTP_SSL;
	return(TRUE);
}


LogToFile(Settings.LogPath,"ERROR: SSL negotiation failed with %s %s. Error was %s",Session->ClientHost,Session->ClientIP,STREAMGetValue(Session->S,"SSL-Error"));
return(FALSE);
}






void HTTPServerFindAndSendDocument(STREAM *S, HTTPSession *Session, int Flags)
{
char *Path=NULL, *ptr;
TPathItem *PI=NULL;
ListNode *Curr;

	if (! VPathProcess(S, Session, Flags))
  {
    ptr=Session->StartDir;
    if (*ptr=='.') ptr++;
    if (strcmp(ptr,"/")==0) Path=CopyStr(Path,Session->Path);
    else Path=MCopyStr(Path,ptr,Session->Path,NULL);

		PI=VPathFind(PATHTYPE_FILETYPE, Session->Path);
		if (PI)
		{
			if (PI->Flags & PATHITEM_COMPRESS) Session->Flags |= FLAG_COMPRESS;
			if (PI->Flags & PATHITEM_NO_COMPRESS) Session->Flags &= ~FLAG_COMPRESS;
			if (PI->CacheTime > 0) Session->CacheTime=PI->CacheTime;
		}

		//One day we will be able to handle scripts inside of chroot using embedded
		//scripting. But not today.
		// if (PI && (PI->Flags & PATHITEM_EXEC)) HTTPServerExecCGI(S, Session, Path);
    //else 
		HTTPServerSendDocument(S, Session, Path, Flags);
  }
DestroyString(Path);
}




void HTTPServerHandleConnection(HTTPSession *Session)
{
char *Tempstr=NULL, *Method=NULL, *URL=NULL;
int AuthOkay=TRUE, result;

if (Settings.Flags & FLAG_SSL)
{
	if (! HTTPServerActivateSSL(Session,Settings.SSLKeys)) return;
}

while (1)
{
if (! HTTPServerReadHeaders(Session)) break;

ProcessSessionEventTriggers(Session);

if (Settings.Flags & FLAG_LOG_MORE_VERBOSE) LogToFile(Settings.LogPath,"PREAUTH: %s against %s %s\n",Session->UserName,Settings.AuthPath,Settings.AuthMethods);
if (Settings.AuthFlags & FLAG_AUTH_REQUIRED)
{
	AuthOkay=FALSE;

	if (HTTPServerAuthenticate(Session))
	{
		LogToFile(Settings.LogPath,"AUTHENTICATED: %s@%s for '%s %s' against %s %s\n",Session->UserName,Session->ClientIP,Session->Method,Session->Path,Settings.AuthPath,Settings.AuthMethods);
		AuthOkay=TRUE;
	}
	else
	{
		if (IsProxyMethod(Session->MethodID)) HTTPServerSendHTML(Session->S, Session, "407 UNAUTHORIZED","Proxy server requires authentication.");
		else HTTPServerSendHTML(Session->S, Session, "401 UNAUTHORIZED","Server requires authentication.");

		if (Session->AuthFlags & FLAG_AUTH_PRESENT) LogToFile(Settings.LogPath,"AUTHENTICATE FAIL: %s@%s for '%s %s' against %s %s\n",Session->UserName,Session->ClientIP,Session->Method,Session->Path,Settings.AuthPath,Settings.AuthMethods);
	}
}




if (! HTTPMethodAllowed(Session)) HTTPServerSendHTML(Session->S, Session, "503 Not implemented","HTTP method disallowed or not implemented.");
else if (! HTTPServerValidateURL(Session, &Tempstr))
{
	HTTPServerSendHTML(Session->S, Session, "403 Forbidden","Bad pattern found in URL");
	LogToFile(Settings.LogPath,"ERROR: Bad pattern '%s' found in URL '%s' from %s@%s (%s)", Tempstr, Session->URL, Session->UserName, Session->ClientHost, Session->ClientIP);
}
else if (AuthOkay)
{

//if this is NOT a session being reused with 'Connection: keepalive' then it must be a new connection
//and so we should setup a new user context 
if (! (Session->Flags & SESSION_REUSE)) HTTPServerSetUserContext(Session);

//We can do this only after we've SetUserContext, as we won't want to
//keep doing it if we're reusing sessions
if (Session->Flags & SESSION_KEEPALIVE) Session->Flags |= SESSION_REUSE;


switch (Session->MethodID)
{
	case METHOD_POST:
	if (! VPathProcess(Session->S, Session, HEADERS_SENDFILE|HEADERS_USECACHE|HEADERS_KEEPALIVE)) HTTPServerHandlePost(Session->S,Session);
	break;

	case METHOD_GET:
		result=HTTPServerProcessActions(Session->S,Session);
		if (! result) HTTPServerFindAndSendDocument(Session->S,Session,HEADERS_SENDFILE|HEADERS_USECACHE|HEADERS_KEEPALIVE);
		break;

	case METHOD_RGET:
	case METHOD_RPOST:
		HTTPProxyRGETURL(Session->S,Session);
		break;

	case METHOD_HEAD:
		HTTPServerFindAndSendDocument(Session->S,Session,HEADERS_KEEPALIVE);
		break;

	case METHOD_PUT:
		HTTPServerRecieveURL(Session->S,Session);
		break;

	case METHOD_MKCOL:
		HTTPServerMkDir(Session->S,Session);
		break;

	case METHOD_DELETE:
		HTTPServerDelete(Session->S,Session);
		break;

	case METHOD_MOVE:
		HTTPServerMove(Session->S,Session);
		break;

	case METHOD_COPY:
		HTTPServerCopy(Session->S,Session);
		break;

	case METHOD_PROPFIND:
		HTTPServerPropFind(Session->S,Session);
		break;

	case METHOD_PROPPATCH:
		HTTPServerPropPatch(Session->S,Session);
		break;

	case METHOD_OPTIONS:
		HTTPServerOptions(Session->S,Session);
		break;

	case METHOD_CONNECT:
		HTTPProxyConnect(Session->S,Session);
		break;

	case METHOD_LOCK:
		HTTPServerHandleLock(Session->S, Session);
		break;

	case METHOD_UNLOCK:
		HTTPServerHandleLock(Session->S, Session);
		break;


	case METHOD_WEBSOCKET:
	case METHOD_WEBSOCKET75:
		WebsocketConnect(Session->S,Session);
		//we can't reuse websocket connections. They are persistent, but they are opaque to 
		//the HTTP server
		Session->Flags &= ~SESSION_REUSE;
		break;


	default:
	HTTPServerSendHTML(Session->S, Session, "503 Not implemented","HTTP method disallowed or not implemented.");
	break;
}
}

LogToFile(Settings.LogPath,"TRANSACTION COMPLETE: %s %s for %s@%s (%s)",Session->Method, Session->Path, Session->UserName,Session->ClientHost,Session->ClientIP);
LogFileFlushAll(TRUE);

STREAMFlush(Session->S);
if (! (Session->Flags & SESSION_REUSE)) break;
//LogToFile(Settings.LogPath,"REUSE: %s %s for %s@%s (%s)",Session->Method, Session->Path, Session->UserName,Session->ClientHost,Session->ClientIP);
}


DestroyString(Tempstr);
DestroyString(Method);
DestroyString(URL);
}

