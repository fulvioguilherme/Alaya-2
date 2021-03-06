#include "proxy.h"
#include "server.h"

int IsProxyMethod(int Method)
{
if (Method==METHOD_RGET) return(TRUE);
if (Method==METHOD_RPOST) return(TRUE);
if (Method==METHOD_CONNECT) return(TRUE);

return(FALSE);
}

void HTTPProxyCopyData(STREAM *Client, STREAM *Target)
{
ListNode *List;
STREAM *S;
char *Buffer=NULL;
int result, total=0;

List=ListCreate();
ListAddItem(List,Client);
ListAddItem(List,Target);

Buffer=SetStrLen(Buffer,BUFSIZ);

while (1)
{
  S=STREAMSelect(List, NULL);

  if (S)
  {
  result=STREAMReadBytes(S,Buffer,BUFSIZ);
  if (result > 0) total+=result;
  if (result ==EOF) break;

  if (S==Client)
  {
    STREAMWriteBytes(Target,Buffer,result);
    STREAMFlush(Client);
  }
  else
  {
    STREAMWriteBytes(Client,Buffer,result);
    STREAMFlush(Target);
  }
  }
}

DestroyString(Buffer);
ListDestroy(List,NULL);
}


void HTTPProxyRGETURL(STREAM *S,HTTPSession *Session)
{
STREAM *TargetS;
char *Tempstr=NULL;
HTTPInfoStruct *Info;
ListNode *Curr;

if (StrLen(Session->Arguments)) Tempstr=MCopyStr(Tempstr,Session->Path,"?",Session->Arguments,NULL);
else Tempstr=CopyStr(Tempstr,Session->Path);

if (Session->MethodID==METHOD_RPOST) 
{
	Info=HTTPInfoFromURL("POST",Tempstr);
	Info->PostContentLength=Session->ContentSize;
	if (StrLen(Session->ContentType))
	{
	Info->PostContentType=CopyStr(Info->PostContentType,Session->ContentType);
	if (StrLen(Session->ContentBoundary)) Info->PostContentType=MCatStr(Info->PostContentType, "; boundary=", Session->ContentBoundary, NULL);
	}
}
else Info=HTTPInfoFromURL("GET",Tempstr);

if (StrLen(Session->RemoteAuthenticate))
{
	SetVar(Info->CustomSendHeaders,"Authorization",Session->RemoteAuthenticate);
}

Curr=ListGetNext(Session->Headers);
while (Curr)
{
	//Don't use 'SetVar' here, as we can have multiple cookie headers
	if (strcasecmp(Curr->Tag,"Cookie")==0) ListAddNamedItem(Info->CustomSendHeaders,"Cookie",CopyStr(NULL,Curr->Item));
	Curr=ListGetNext(Curr);
}

Info->Flags |= HTTP_NODECODE | HTTP_NOCOOKIES;

//We are probably chrooted and thus unable to do DNS lookups.
//As parent process to do it for us
/*
if (ParentProcessPipe)
{
Tempstr=MCopyStr(Tempstr,"GETIP"," ",Info->Host,"\n",NULL);
STREAMWriteLine(Tempstr,ParentProcessPipe);
STREAMFlush(ParentProcessPipe);
Tempstr=STREAMReadLine(Tempstr,ParentProcessPipe);
StripTrailingWhitespace(Tempstr);
LogToFile(Settings.LogPath,"GOTIP: %s\n",Tempstr);
if (StrLen(Tempstr)) Info->Host=CopyStr(Info->Host,Tempstr);
}
*/



TargetS=HTTPTransact(Info);

if (TargetS)
{
	LogToFile(Settings.LogPath,"PROXY: Connected To %s",Session->Path);
	//Must send POST data before doing anything else
	if (Session->ContentSize > 0)
	{
  STREAMSendFile(S, TargetS, Session->ContentSize, SENDFILE_LOOP);
	//we shouldn't need this CR-LF, as we've sent 'Content-Length' characters
	//but some CGI implementations seem to expect it, and it does no harm to
	//provide it anyway
	STREAMWriteLine("\r\n", TargetS);
	STREAMFlush(TargetS);

	//For POST we must call transact again
	HTTPTransact(Info);
	}

	STREAMWriteLine("HTTP/1.1 200 OK Connection Established\r\n",S);
	Curr=ListGetNext(Info->ServerHeaders);
	while (Curr)
	{
		Tempstr=MCopyStr(Tempstr,Curr->Tag,": ",Curr->Item,"\r\n",NULL);
		STREAMWriteLine(Tempstr,S);
		Curr=ListGetNext(Curr);
	}
	STREAMWriteLine("Connection: close\r\n",S);
	STREAMWriteLine("\r\n",S);


	//Still have to do this, it's a two-way copy
	HTTPProxyCopyData(S, TargetS);
}
else
{
		LogToFile(Settings.LogPath,"PROXY: Connect FAILED to %s",Session->Path);
		STREAMWriteLine("HTTP/1.1 502 Connection Failed\r\n",S);
		Tempstr=MCopyStr(Tempstr, "Server: Alaya/",Version,"\r\n",NULL);
		STREAMWriteLine(Tempstr,S);
		Tempstr=CopyStr(Tempstr,GetDateStr("Date: %a, %d %b %Y %H:%M:%S %Z\r\n",NULL));
		STREAMWriteLine(Tempstr,S);
		STREAMWriteLine("Connection: close\r\n",S);
		STREAMWriteLine("\r\n",S);
}
STREAMClose(TargetS);

DestroyString(Tempstr);
}


void HTTPProxyConnect(STREAM *S,HTTPSession *ClientHeads)
{
STREAM *TargetS=NULL;
int Port=0;
char *Host=NULL, *Date=NULL, *Tempstr=NULL, *ptr;

//Path will normally start with a '/', remove it
ptr=ClientHeads->Path;
if (*ptr=='/') ptr++;

Host=CopyStr(Host,ptr);

ptr=strrchr(Host,':');
if (ptr)
{
	*ptr='\0';
	ptr++;
	Port=atoi(ptr);
}

Date=CopyStr(Date,GetDateStr("Date: %a, %d %b %Y %H:%M:%S %Z\r\n",NULL));
if (Port > 0)
{
	TargetS=STREAMCreate();
	LogToFile(Settings.LogPath,"HTTP CONNECT: [%s] [%d]",Host,Port);
	if (STREAMConnectToHost(TargetS,Host,Port,0))
	{
		STREAMWriteLine("HTTP/1.1 200 OK Connection Established\r\n",S);
		Tempstr=MCopyStr(Tempstr, "Server: Alaya/",Version,"\r\n",NULL);
		STREAMWriteLine(Tempstr,S);
		STREAMWriteLine(Date,S);
		STREAMWriteLine("Connection: close\r\n",S);
		STREAMWriteLine("\r\n",S);
		STREAMFlush(S);

		HTTPProxyCopyData(S,TargetS);
	}
	else
	{
		STREAMWriteLine("HTTP/1.1 502 Connection Failed\r\n",S);
		Tempstr=MCopyStr(Tempstr, "Server: Alaya/",Version,"\r\n",NULL);
		STREAMWriteLine(Tempstr,S);
		STREAMWriteLine(Date,S);
		STREAMWriteLine("Connection: close\r\n",S);
		STREAMWriteLine("Content-Length: 0\r\n\r\n",S);
	}
}
else
{
	STREAMWriteLine("HTTP/1.1 400 No port given\r\n",S);
	Tempstr=MCopyStr(Tempstr, "Server: Alaya/",Version,"\r\n",NULL);
	STREAMWriteLine(Tempstr,S);
	STREAMWriteLine(Date,S);
	STREAMWriteLine("Connection: close\r\n",S);
	STREAMWriteLine("Content-Length: 0\r\n\r\n",S);
}
STREAMClose(TargetS);

DestroyString(Tempstr);
DestroyString(Host);
DestroyString(Date);
}


