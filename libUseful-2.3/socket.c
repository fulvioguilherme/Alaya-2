#include "socket.h"
#include "ConnectionChain.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <unistd.h>


int IsIP4Address(const char *Str)
{
const char *ptr;
int dot_count=0;
int AllowDot=FALSE;

if (! Str) return(FALSE);

for (ptr=Str; *ptr != '\0'; ptr++)
{
	if (*ptr == '.') 
	{
		if (! AllowDot) return(FALSE);
		dot_count++;
		AllowDot=FALSE;
	}
	else 
	{
		if (! isdigit(*ptr)) return(FALSE);
		AllowDot=TRUE;
	}
}

if (dot_count != 3) return(FALSE);

return(TRUE);
}


int IsIP6Address(const char *Str)
{
const char *ptr;
const char *IP6CHARS="0123456789abcdefABCDEF:%";

if (!Str) return(FALSE);

for (ptr=Str; *ptr != '\0'; ptr++)
{
 if (*ptr=='%') break;
 if (! strchr(IP6CHARS,*ptr)) return(FALSE);
}

return(TRUE);
}




/* This is a simple function to decide if a string is an IP address as   */
/* opposed to a host/domain name.                                        */

int IsIPAddress(const char *Str)
{
int len,count;
len=strlen(Str);
if (len <1) return(FALSE);
for (count=0; count < len; count++)
   if ((! isdigit(Str[count])) && (Str[count] !='.')) return(FALSE);
 return(TRUE);
}




int IsSockConnected(int sock)
{
struct sockaddr_in sa;
socklen_t salen;
int result;

if (sock==-1) return(FALSE);
salen=sizeof(sa);
result=getpeername(sock,(struct sockaddr *) &sa, &salen);
if (result==0) return(TRUE);
if (errno==ENOTCONN) return(SOCK_CONNECTING);
return(FALSE);
}


int SockSetOptions(int sock, int SetFlags, int UnsetFlags)
{
int result;

result=TRUE;
if (SetFlags & SOCK_BROADCAST) result=setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &result,sizeof(int));

if (SetFlags & SOCK_DONTROUTE) result=setsockopt(sock, SOL_SOCKET, SO_DONTROUTE, &result,sizeof(int));

if (SetFlags & SOCK_REUSEPORT) result=setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &result,sizeof(int));

if (SetFlags & SOCK_TPROXY) result=setsockopt(sock, IPPROTO_IP, IP_TRANSPARENT, &result,sizeof(int));

if (SetFlags & SOCK_PEERCREDS) result=setsockopt(sock, SOL_SOCKET, SO_PASSCRED, &result,sizeof(int));

//Default is KEEPALIVE ON
setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &result,sizeof(int));

if (SetFlags & CONNECT_NONBLOCK) fcntl(sock,F_SETFL,O_NONBLOCK);


result=FALSE;
if (SetFlags & SOCK_NOKEEPALIVE) setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &result,sizeof(int));

if (UnsetFlags & SOCK_BROADCAST) result=setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &result,sizeof(int));

if (UnsetFlags & SOCK_DONTROUTE) result=setsockopt(sock, SOL_SOCKET, SO_DONTROUTE, &result,sizeof(int));

if (UnsetFlags & SOCK_REUSEPORT) result=setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &result,sizeof(int));

if (UnsetFlags & SOCK_TPROXY) result=setsockopt(sock, IPPROTO_IP, IP_TRANSPARENT, &result,sizeof(int));

if (UnsetFlags & SOCK_PEERCREDS) result=setsockopt(sock, SOL_SOCKET, SO_PASSCRED, &result,sizeof(int));
}


int SockSetAttribute(int sock, int Attrib, int Value)
{


}

int SockGetAttribute(int sock, int Attrib, int Value)
{


}



int GetHostARP(const char *IP, char **Device, char **MAC)
{
char *Tempstr=NULL, *Token=NULL;
int result=FALSE;
const char *ptr;
FILE *F;

Tempstr=SetStrLen(Tempstr, 255);
F=fopen("/proc/net/arp","r");
if (F)
{
	*Device=CopyStr(*Device,"remote");
	*MAC=CopyStr(*MAC,"remote");
	//Read Title Line
	fgets(Tempstr,255,F);

	while (fgets(Tempstr,255,F))
	{
		StripTrailingWhitespace(Tempstr);
		ptr=GetToken(Tempstr," ",&Token,0);
		if (strcmp(Token,IP)==0)
		{
			while (isspace(*ptr)) ptr++;
			ptr=GetToken(ptr," ",&Token,0);

			while (isspace(*ptr)) ptr++;
			ptr=GetToken(ptr," ",&Token,0);

			while (isspace(*ptr)) ptr++;
			ptr=GetToken(ptr," ",MAC,0);
			strlwr(*MAC);

			while (isspace(*ptr)) ptr++;
			ptr=GetToken(ptr," ",&Token,0);

			while (isspace(*ptr)) ptr++;
			ptr=GetToken(ptr," ",Device,0);

			result=TRUE;
		}
	}
fclose(F);
}

DestroyString(Tempstr);
DestroyString(Token);

return(result);
}




const char *GetInterfaceIP(const char *Interface)
{
 int fd, result;
 struct ifreq ifr;

 fd = socket(AF_INET, SOCK_DGRAM, 0);
 if (fd==-1) return("");

 ifr.ifr_addr.sa_family = AF_INET;
 strncpy(ifr.ifr_name, Interface, IFNAMSIZ-1);
 result=ioctl(fd, SIOCGIFADDR, &ifr);
 if (result==-1) return("");
 close(fd);

 return(inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
}


#ifdef USE_INET6
int InitServerSock(int Type, const char *Address, int Port)
{
int sock;
struct sockaddr_storage sa;
struct sockaddr_in *sa4;
struct sockaddr_in6 *sa6;
socklen_t salen;
int result;
const char *p_Addr=NULL, *ptr;
char *Token=NULL;


if (StrLen(Address))  
{
	if (IsIP4Address(Address)) 
	{
		sa.ss_family=AF_INET;
		p_Addr=Address;
	}
	else if (IsIP6Address(Address)) 
	{
		sa.ss_family=AF_INET6;
		p_Addr=Address;
	}
	else 
	{
		sa.ss_family=AF_INET;
		p_Addr=GetInterfaceIP(Address);
	}
}
else sa.ss_family=AF_INET6;

if (sa.ss_family==AF_INET)
{
	sa4=(struct sockaddr_in *) &sa;
	if (StrLen(p_Addr)) sa4->sin_addr.s_addr=StrtoIP(p_Addr);
	else sa4->sin_addr.s_addr=INADDR_ANY;
	sa4->sin_port=htons(Port);
	sa4->sin_family=AF_INET;
	salen=sizeof(struct sockaddr_in);
}
else
{
	sa6=(struct sockaddr_in6 *) &sa;
	sa6->sin6_port=htons(Port);
	sa6->sin6_family=AF_INET6;
	if (StrLen(p_Addr)) 
	{
		ptr=GetToken(p_Addr, "%",&Token,0);
		if (StrLen(ptr)) sa6->sin6_scope_id=if_nametoindex(ptr);
		inet_pton(AF_INET6, Token, &(sa6->sin6_addr));
		DestroyString(Token);
	}
	else sa6->sin6_addr=in6addr_any;
	salen=sizeof(struct sockaddr_in6);
}

if (Type==0) Type=SOCK_STREAM;
sock=socket(sa.ss_family,Type,0);
if (sock <0) return(-1);

//No reason to pass server/listen sockets across an exec
fcntl(sock, F_SETFD, FD_CLOEXEC);

result=1;
setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&result,sizeof(result));

result=bind(sock,(struct sockaddr *) &sa, salen);

if ((result==0) && (Type==SOCK_STREAM))
{
 result=listen(sock,10);
}

if (result==0) return(sock);
else 
{
close(sock);
return(-1);
}
}

#else 

int InitServerSock(int Type, const char *Address, int Port)
{
struct sockaddr_in sa;
int result;
const char *ptr;
socklen_t salen;
int sock;

sa.sin_port=htons(Port);
sa.sin_family=AF_INET;


if (StrLen(Address) > 0) 
{
	if (IsIP6Address(Address)) return(-1);

	if (IsIP4Address(Address)) ptr=Address;
	else ptr=GetInterfaceIP(Address);
	sa.sin_addr.s_addr=StrtoIP(ptr);
}
else sa.sin_addr.s_addr=INADDR_ANY;

if (Type==0) Type=SOCK_STREAM;
sock=socket(AF_INET, Type,0);
if (sock <0) return(-1);

//No reason to pass server/listen sockets across an exec
fcntl(sock, F_SETFD, FD_CLOEXEC);

result=1;
salen=sizeof(result);
setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&result,salen);


salen=sizeof(struct sockaddr_in);
result=bind(sock,(struct sockaddr *) &sa, salen);

if ((result==0) && (Type==SOCK_STREAM))
{
 result=listen(sock,10);
}

if (result==0) return(sock);
else 
{
close(sock);
return(-1);
}
}
#endif


int InitUnixServerSock(int Type, const char *Path)
{
int sock;
struct sockaddr_un sa;
socklen_t salen;
int result;

if (Type==0) Type=SOCK_STREAM;
sock=socket(AF_UNIX, Type, 0);
if (sock <0) return(-1);

//No reason to pass server/listen sockets across an exec
fcntl(sock, F_SETFD, FD_CLOEXEC);

result=1;
salen=sizeof(result);
strcpy(sa.sun_path,Path);
sa.sun_family=AF_UNIX;
salen=sizeof(struct sockaddr_un);
result=bind(sock,(struct sockaddr *) &sa, salen);

if ((result==0) && (Type==SOCK_STREAM))
{
 result=listen(sock,10);
}

if (result==0) return(sock);
else 
{
close(sock);
return(-1);
}
}


int TCPServerSockAccept(int ServerSock, char **Addr)
{
struct sockaddr_storage sa;
socklen_t salen;
int sock;

salen=sizeof(sa);
sock=accept(ServerSock,(struct sockaddr *) &sa, &salen);
if (Addr && (sock > -1)) 
{
*Addr=SetStrLen(*Addr,NI_MAXHOST);
getnameinfo((struct sockaddr *) &sa, salen, *Addr, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
}
return(sock);
}


int UnixServerSockAccept(int ServerSock)
{
struct sockaddr_un sa;
socklen_t salen;
int sock;

salen=sizeof(sa);
sock=accept(ServerSock,(struct sockaddr *) &sa,&salen);
return(sock);
}







#ifdef USE_INET6
int GetSockDetails(int sock, char **LocalAddress, int *LocalPort, char **RemoteAddress, int *RemotePort)
{
socklen_t salen;
int result;
struct sockaddr_storage sa;
char *Tempstr=NULL, *ptr;

*LocalPort=0;
*RemotePort=0;
*LocalAddress=CopyStr(*LocalAddress,"");
*RemoteAddress=CopyStr(*RemoteAddress,"");

salen=sizeof(struct sockaddr_storage);
result=getsockname(sock, (struct sockaddr *) &sa, &salen);

if (result==0)
{
	*LocalAddress=SetStrLen(*LocalAddress,NI_MAXHOST);
	Tempstr=SetStrLen(Tempstr,NI_MAXSERV);
	getnameinfo((struct sockaddr *) &sa, salen, *LocalAddress, NI_MAXHOST, Tempstr, NI_MAXSERV, NI_NUMERICHOST|NI_NUMERICSERV);
	*LocalPort=atoi(Tempstr);

	if ((strncmp(*LocalAddress,"::ffff:",7)==0) && strchr(*LocalAddress,'.')) 
	{
		ptr=*LocalAddress;
		ptr+=7;
		memmove(*LocalAddress,ptr,StrLen(*LocalAddress)-6);
	}
	

	//Set Address to be the same as control sock, as it might not be INADDR_ANY
	result=getpeername(sock, (struct sockaddr *) &sa, &salen);

	if (result==0)
	{
		*RemoteAddress=SetStrLen(*RemoteAddress,NI_MAXHOST);
		Tempstr=SetStrLen(Tempstr,NI_MAXSERV);
		getnameinfo((struct sockaddr *) &sa, salen, *RemoteAddress, NI_MAXHOST, Tempstr, NI_MAXSERV, NI_NUMERICHOST|NI_NUMERICSERV);
		*RemotePort=atoi(Tempstr);

		if ((strncmp(*RemoteAddress,"::ffff:",7)==0) && strchr(*RemoteAddress,'.')) 
		{
		ptr=*RemoteAddress;
		ptr+=7;
		memmove(*RemoteAddress,ptr,StrLen(*RemoteAddress)-6);
		}
	}

	//We've got the local sock, so lets still call it a success
	result=0;
}

DestroyString(Tempstr);

if (result==0) return(TRUE);
return(FALSE);
}

#else

int GetSockDetails(int sock, char **LocalAddress, int *LocalPort, char **RemoteAddress, int *RemotePort)
{
socklen_t salen;
int result;
struct sockaddr_in sa;

*LocalPort=0;
*RemotePort=0;
*LocalAddress=CopyStr(*LocalAddress,"");
*RemoteAddress=CopyStr(*RemoteAddress,"");

salen=sizeof(struct sockaddr_in);
result=getsockname(sock, (struct sockaddr *) &sa, &salen);

if (result==0)
{
	*LocalAddress=CopyStr(*LocalAddress,IPtoStr(sa.sin_addr.s_addr));
	*LocalPort=ntohs(sa.sin_port);

	//Set Address to be the same as control sock, as it might not be INADDR_ANY
	result=getpeername(sock, (struct sockaddr *) &sa, &salen);

	if (result==0)
	{
		*RemoteAddress=CopyStr(*RemoteAddress,IPtoStr(sa.sin_addr.s_addr));
		*RemotePort=sa.sin_port;
	}

	//We've got the local sock, so lets still call it a success
	result=0;
}

if (result==0) return(TRUE);
return(FALSE);
}

#endif




/* Users will probably only use this function if they want to reconnect   */
/* a broken connection, or reuse a socket for multiple connections, hence */
/* the name... */
int ReconnectSock(int sock, const char *Host, int Port, int Flags)
{
socklen_t salen;
int result;
struct sockaddr_in sa;
struct hostent *hostdata;

sa.sin_family=AF_INET;
sa.sin_port=htons(Port);

if (IsIP4Address(Host))
{
inet_aton(Host, (struct in_addr *) &sa.sin_addr);
}
else 
{ 
   hostdata=gethostbyname(Host);
   if (!hostdata) 
   {
     return(-1);
   }
sa.sin_addr=*(struct in_addr *) *hostdata->h_addr_list;
}

salen=sizeof(sa);
SockSetOptions(sock, Flags, 0);

result=connect(sock,(struct sockaddr *)&sa, salen);
if (result==0) result=TRUE;

if ((result==-1) && (Flags & CONNECT_NONBLOCK) && (errno == EINPROGRESS)) result=FALSE;
return(result);
}


int ConnectToHost(const char *Host, int Port,int Flags)
{
int sock, result;

sock=socket(AF_INET,SOCK_STREAM,0);
if (sock <0) return(-1);
result=ReconnectSock(sock,Host,Port,Flags);
if (result==-1)
{
close(sock);
return(-1);
}

return(sock);

}




int CheckForTerm(DownloadContext *CTX, char inchar)
{

    if (inchar == CTX->TermStr[CTX->TermPos]) 
    {
        CTX->TermPos++;
        if (CTX->TermPos >=strlen(CTX->TermStr)) 
        {
           CTX->TermPos=0;
           return(TRUE);
        }
    }
    else
    {
      if (CTX->TermPos >0) 
      {
         STREAMWriteBytes(CTX->Output, CTX->TermStr,CTX->TermPos);
         CTX->TermPos=0;
         return(CheckForTerm(CTX,inchar));
      }
    }
return(FALSE); 
}



int  ProcessIncommingBytes(DownloadContext *CTX)
{
int inchar, FoundTerm=FALSE;

 inchar=STREAMReadChar(CTX->Input);

 if (inchar==EOF) return(TRUE);

 FoundTerm=CheckForTerm(CTX,(char) inchar);
 while ((inchar !=EOF) && (! FoundTerm))
 {
     if (CTX->TermPos==0) STREAMWriteChar(CTX->Output, (char) inchar);
     inchar=STREAMReadChar(CTX->Input);
     FoundTerm=CheckForTerm(CTX, (char) inchar);
 }
if (inchar==EOF) return(TRUE);
if (FoundTerm) return(TRUE);
return(FALSE);
}


int DownloadToTermStr2(STREAM *Connection, STREAM *SaveFile, char *TermStr)
{
DownloadContext CTX;

CTX.TermStr=CopyStr(NULL,TermStr);
CTX.Input=Connection;
CTX.Output=SaveFile;
CTX.TermPos=0;

while(ProcessIncommingBytes(&CTX) !=TRUE);

DestroyString(CTX.TermStr);
return(TRUE);
}

int DownloadToDot(STREAM *Connection, STREAM *SaveFile)
{
return(DownloadToTermStr2(Connection,SaveFile,"\r\n.\r\n"));
}


int DownloadToTermStr(STREAM *Connection, STREAM *SaveFile, char *TermStr)
{
char *Tempstr=NULL;

Tempstr=STREAMReadLine(Tempstr,Connection);
while (Tempstr)
{
  if (strcmp(Tempstr,TermStr)==0)
{
 break;
}
  STREAMWriteLine(Tempstr,SaveFile);
  Tempstr=STREAMReadLine(Tempstr,Connection);
}
return(TRUE);
}



char *LookupHostIP(const char *Host)
{
struct hostent *hostdata;

   hostdata=gethostbyname(Host);
   if (!hostdata) 
   {
     return(NULL);
   }

//inet_ntoa shouldn't need this cast to 'char *', but it emitts a warning
//without it
return((char *) inet_ntoa(*(struct in_addr *) *hostdata->h_addr_list));
}




char *GetRemoteIP(int sock)
{
struct sockaddr_in sa;
socklen_t salen;
int result;

salen=sizeof(struct sockaddr_in);
result=getpeername(sock,(struct sockaddr *) &sa, &salen);
if  (result==-1)  
{
if (errno==ENOTSOCK)  return("127.0.0.1");
else return("0.0.0.0");
}

return((char *) inet_ntoa(sa.sin_addr));
}


char *IPStrToHostName(const char *IPAddr)
{
struct sockaddr_in sa;
struct hostent *hostdata=NULL;

inet_aton(IPAddr,& sa.sin_addr);
hostdata=gethostbyaddr(&sa.sin_addr.s_addr,sizeof((sa.sin_addr.s_addr)),AF_INET);
if (hostdata) return(hostdata->h_name);
else return("");
}




char *IPtoStr(unsigned long Address)
{
struct sockaddr_in sa;
sa.sin_addr.s_addr=Address;
return((char *) inet_ntoa(sa.sin_addr));

}

unsigned long StrtoIP(const char *Str)
{
struct sockaddr_in sa;
if (inet_aton(Str,&sa.sin_addr)) return(sa.sin_addr.s_addr);
return(0);
}


int STREAMIsConnected(STREAM *S)
{
int result=FALSE;

if (! S) return(FALSE);
result=IsSockConnected(S->in_fd);
if (result==TRUE)
{
	if (S->State & SS_CONNECTING)
	{
		S->State |= SS_CONNECTED;
		S->State &= (~SS_CONNECTING);
	}
}
if ((result==SOCK_CONNECTING) && (! (S->State & SS_CONNECTING))) result=FALSE;
return(result);
}



int STREAMDoPostConnect(STREAM *S, int Flags)
{
int result=FALSE;
char *ptr;
struct timeval tv;

if (! S) return(FALSE);
if ((S->in_fd > -1) && (S->Timeout > 0) )
{
  tv.tv_sec=S->Timeout;
  tv.tv_usec=0;
  if (FDSelect(S->in_fd, SELECT_WRITE, &tv) != SELECT_WRITE)
  {
    close(S->in_fd);
    S->in_fd=-1;
    S->out_fd=-1;
  }
}

if (S->in_fd > -1)
{
//We will have been non-blocking during connection, but if we don't 
//really want the stream to be non blocking, we unset that here
if (! (Flags & CONNECT_NONBLOCK))  STREAMSetFlags(S, 0, SF_NONBLOCK);

S->Type=STREAM_TYPE_TCP;
result=TRUE;
STREAMSetFlushType(S,FLUSH_LINE,0,0);
//if (Flags & CONNECT_SOCKS_PROXY) result=DoSocksProxyTunnel(S);
if (Flags & CONNECT_SSL) DoSSLClientNegotiation(S, Flags);

ptr=GetRemoteIP(S->in_fd);
if (ptr) STREAMSetValue(S,"PeerIP",ptr);
}

return(result);
}



int STREAMConnectToHost(STREAM *S, const char *DesiredHost, int DesiredPort,int Flags)
{
ListNode *Curr;
char *Token=NULL;
int result=FALSE;
int HopNo=0, val=0;
ListNode *LastHop=NULL;

S->Path=FormatStr(S->Path,"tcp:%s:%d",DesiredHost,DesiredPort);
//Find the last hop, used to decide what ssh command to use
Curr=ListGetNext(S->Values);
while (Curr)
{
GetToken(Curr->Tag,":",&Token,0);
if (strcasecmp(Token,"ConnectHop")==0) LastHop=Curr;
Curr=ListGetNext(Curr);
}

STREAMSetFlushType(S,FLUSH_LINE,0,0);
Curr=ListGetNext(S->Values);
while (Curr)
{
GetToken(Curr->Tag,":",&Token,0);

if (strcasecmp(Token,"ConnectHop")==0) result=STREAMProcessConnectHop(S, (char *) Curr->Item,Curr==LastHop);

HopNo++;
if (! result) break;
Curr=ListGetNext(Curr);
}

//If we're not handling the connection through 'Connect hops' then
//just connect to host
if ((HopNo==0) && StrLen(DesiredHost))
{
	if (Flags & CONNECT_NONBLOCK) S->Flags |= SF_NONBLOCK;
	
	//Flags are handled in this function
	S->in_fd=ConnectToHost(DesiredHost,DesiredPort,Flags);

	S->out_fd=S->in_fd;
	if (S->in_fd > -1) result=TRUE;
}

if (result==TRUE)
{
	if (Flags & CONNECT_NONBLOCK) 
	{
		S->State |=SS_CONNECTING;
		S->Flags |=SF_NONBLOCK;
	}
	else
	{
		S->State |=SS_CONNECTED;
		result=STREAMDoPostConnect(S, Flags);
	}
}


return(result);
}





int OpenUDPSock(int Port, int Flags)
{
	int result;
	struct sockaddr_in addr;
	int fd;

	addr.sin_family=AF_INET;
//	addr.sin_addr.s_addr=Interface;
	addr.sin_addr.s_addr=INADDR_ANY;
	addr.sin_port=htons(Port);

	fd=socket(AF_INET, SOCK_DGRAM,0);
	result=bind(fd,(struct sockaddr *) &addr, sizeof(addr));
	if (result !=0)
	{
		close(fd);
		return(-1);
	}

	SockSetOptions(fd, Flags, 0);
	return(fd);
}


STREAM *STREAMOpenUDP(int Port, int Flags)
{
int fd;
STREAM *Stream;

fd=OpenUDPSock(Port, Flags);
if (fd <0) return(NULL);
Stream=STREAMFromFD(fd);
Stream->Path=FormatStr(Stream->Path,"udp::%d",Port);
Stream->Type=STREAM_TYPE_UDP;
return(Stream);
}



int STREAMSendDgram(STREAM *S, const char *Host, int Port, char *Bytes, int len)
{
struct sockaddr_in sa;
socklen_t salen;
struct hostent *hostdata;

sa.sin_port=htons(Port);
sa.sin_family=AF_INET;
inet_aton(Host,& sa.sin_addr);
salen=sizeof(sa);

if (IsIP4Address(Host))
{
   inet_aton(Host, (struct in_addr *) &sa.sin_addr);
}
else 
{ 
   hostdata=gethostbyname(Host);
   if (!hostdata) 
   {
     return(-1);
   }
sa.sin_addr=*(struct in_addr *) *hostdata->h_addr_list;
}


return(sendto(S->out_fd,Bytes,len,0,(struct sockaddr *) &sa,salen));
}



