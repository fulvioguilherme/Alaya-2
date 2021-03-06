#include "Time.h"

//we cache seconds because we expect most questions about
//time to be in seconds, and this avoids multiplying millisecs up
time_t CachedTime=0;
//This is cached millisecs since 1970
uint64_t CachedMillisecs=0;



uint64_t GetTime(int Flags)
{
struct timeval tv;

if ((! (Flags & TIME_CACHED)) || (CachedTime==0))
{
 gettimeofday(&tv, NULL);
 CachedTime=tv.tv_sec;
 CachedMillisecs=(tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

if (Flags & TIME_MILLISECS) return(CachedMillisecs);
else if (Flags & TIME_CENTISECS) return(CachedMillisecs * 10);
return((uint64_t) CachedTime);
}


char *GetDateStrFromSecs(const char *DateFormat, time_t Secs, const char *TimeZone)
{
time_t val;
struct tm *TMS;
static char *Buffer=NULL;
char *Tempstr=NULL;
#define DATE_BUFF_LEN 255

val=Secs;

if (StrLen(TimeZone))
{
if (getenv("TZ")) Tempstr=CopyStr(Tempstr,getenv("TZ"));
setenv("TZ",TimeZone,TRUE);
tzset();
}
TMS=localtime(&val);
if (StrLen(TimeZone))
{
if (! Tempstr) unsetenv("TZ");
else setenv("TZ",Tempstr,TRUE);
tzset();
}

val=StrLen(DateFormat)+ DATE_BUFF_LEN;
Buffer=SetStrLen(Buffer,val);
strftime(Buffer,val,DateFormat,TMS);

DestroyString(Tempstr);
return(Buffer);
}



char *GetDateStr(const char *DateFormat, const char *TimeZone)
{
time(&CachedTime);
return(GetDateStrFromSecs(DateFormat, CachedTime, TimeZone));
}


time_t DateStrToSecs(const char *DateFormat, const char *Str, const char *TimeZone)
{
time_t Secs=0;
struct tm TMS;
char *Tempstr=NULL;

if (StrLen(DateFormat)==0) return(0);
if (StrLen(Str)==0) return(0);

if (StrLen(TimeZone))
{
	if (getenv("TZ")) Tempstr=CopyStr(Tempstr,getenv("TZ"));
	setenv("TZ",TimeZone,TRUE);
	tzset();
}

strptime(Str,DateFormat,&TMS);
TMS.tm_isdst=-1;
Secs=mktime(&TMS);

if (StrLen(TimeZone))
{
	if (! Tempstr) unsetenv("TZ");
	else setenv("TZ",Tempstr,TRUE);
	tzset();
}
return(Secs);
}



/* A general 'Set Timer' function, Useful for timing out */
/* socket connections etc                                */
void SetTimeout(int timeout)
{
struct sigaction SigAct;

SigAct.sa_handler=&ColLibDefaultSignalHandler;
SigAct.sa_flags=SA_RESETHAND;
//SigAct.sa_restorer=NULL;

sigaction(SIGALRM,&SigAct,NULL);
alarm(timeout);
}

