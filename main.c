//Alaya, webdav server
//Named after 'Alaya-vijnana', the Buddhist concept of 'storehouse mind' that 
//'receives impressions from all functions of the other consciousnesses and retains them 
//as potential energy for their further manifestations and activities.'
//Written by Colum Paget.
//Copyright 2011 Colum Paget.


/****  Gnu Public Licence ****/
/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common.h"
#include "Authenticate.h"
#include "Settings.h"
#include "server.h"


ListNode *MimeTypes=NULL;
ListNode *Connections=NULL;
STREAM *ParentProcessPipe=NULL;

void SetTimezoneEnv()
{
time_t Now;

time(&Now);
localtime(&Now);

printf("TZ: %s %s\n",tzname[1],tzname[0]);
if (StrLen(tzname[1]))
{
   setenv("TZ",tzname[1],TRUE);
}
else if (StrLen(tzname[0]))
{
   setenv("TZ",tzname[0],TRUE);
}
}



int ChildFunc(void *Data)
{
HTTPSession *Session;

		ParentProcessPipe=STREAMFromDualFD(0,1);
		Session=(HTTPSession *) Data;
		STREAMSetFlushType(Session->S,FLUSH_FULL,4096);
		if (Settings.Flags & FLAG_SSL) ActivateSSL(Session->S,Settings.SSLKeys);
		HTTPServerHandleConnection(Session);

		STREAMClose(Session->S);
		HTTPSessionDestroy(Session);

		STREAMClose(ParentProcessPipe);
		LogFileFlushAll(TRUE);
		_exit(0);
}

void AcceptConnection(int ServiceSock)
{
int fd, infd, outfd, errfd;
char *Tempstr=NULL, *ptr;
HTTPSession *Session;
STREAM *S;
int pid;

		//Check if log file has gotten big and rotate if needs be
		LogFileCheckRotate(Settings.LogPath);

		Session=HTTPSessionCreate();
		fd=TCPServerSockAccept(ServiceSock,&Session->ClientIP);
		Session->S=STREAMFromFD(fd);
		STREAMSetFlushType(Session->S, FLUSH_FULL,0);

		pid=PipeSpawnFunction(&infd,&outfd,NULL, ChildFunc, Session);
		Tempstr=FormatStr(Tempstr,"%d",pid);
		S=STREAMFromDualFD(outfd, infd);
		ListAddNamedItem(Connections,Tempstr,S);

	//Closes 'fd' too
	STREAMClose(Session->S);
	HTTPSessionDestroy(Session);
	DestroyString(Tempstr);
}


void CollectChildProcesses()
{
int i, pid;
char *Tempstr=NULL;
ListNode *Curr;

	for (i=0; i < 20; i++) 
	{
		pid=waitpid(-1,NULL,WNOHANG);
		if (pid==-1) break;

		Tempstr=FormatStr(Tempstr,"%d",pid);
		Curr=ListFindNamedItem(Connections,Tempstr);
		if (Curr)
		{
			STREAMClose((STREAM *) Curr->Item);
			ListDeleteNode(Curr);
		}
	}
DestroyString(Tempstr);
}



void SetGMT()
{
time_t Now;

setenv("TZ","GMT",TRUE);
time(&Now);
localtime(&Now);
}


main(int argc, char *argv[])
{
STREAM *ServiceSock, *S;
int fd;
char *Tempstr=NULL;
int result;


SetTimezoneEnv();

//Drop most capabilities
DropCapabilities(CAPS_LEVEL_STARTUP);
nice(10);
InitSettings();

if (StrLen(Settings.Timezone)) setenv("TZ",Settings.Timezone,TRUE);
//LibUsefulMemFlags |= MEMORY_CLEAR_ONFREE;

openlog("alaya",LOG_PID, LOG_DAEMON);

Connections=ListCreate();


ParseSettings(argc,argv,&Settings);
ReadConfigFile(&Settings);
ParseSettings(argc,argv,&Settings);
PostProcessSettings(&Settings);

if (Settings.Flags & FLAG_SSL_PFS) 
{
	//if (! StrLen(LibUsefulGetValue("SSL-DHParams-File"))) OpenSSLGenerateDHParams();
}

//Do this before any logging!
if (! (Settings.Flags & FLAG_NODEMON)) demonize();


LogFileSetValues(Settings.LogPath, LOGFILE_LOGPID|LOGFILE_LOCK|LOGFILE_MILLISECS, Settings.MaxLogSize, 10);
LogToFile(Settings.LogPath, "Alaya starting up. Version %s",Version);

LoadFileMagics("/etc/mime.types","/etc/magic");

fd=InitServerSock(Settings.BindAddress,Settings.Port);
if (fd==-1)
{
	LogToFile(Settings.LogPath, "Cannot bind to port %s:%d",Settings.BindAddress,Settings.Port);
	printf("Cannot bind to port %s:%d\n",Settings.BindAddress,Settings.Port);
	exit(1);
}


Tempstr=FormatStr(Tempstr,"alaya-%s-port%d",Settings.BindAddress,Settings.Port);
WritePidFile(Tempstr);

ServiceSock=STREAMFromFD(fd);
ListAddItem(Connections,ServiceSock);

//We no longer need the 'bind port' capablity
DropCapabilities(CAPS_LEVEL_NETBOUND);

while (1)
{
S=STREAMSelect(Connections,NULL);

if (S)
{
	if (S==ServiceSock) AcceptConnection(S->in_fd);
	else 
	{
		result=HandleChildProcessRequest(S);
		if (! result) 
		{
			ListDeleteItem(Connections,S);
			STREAMClose(S);
		}
	}
}

CollectChildProcesses();
}

LogFileFlushAll(TRUE);
DestoryString(Tempstr);
}
