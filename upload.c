#include "upload.h"
#include "server.h"

#define UPLOAD_DONE 1
#define UPLOAD_UNPACK 2

int UploadReadMultipartHeaders(STREAM *S, char **Field, char **FileName)
{
char *Tempstr=NULL, *Name=NULL, *Value=NULL, *ptr;
int result=FALSE;

Tempstr=STREAMReadLine(Tempstr,S);
while (StrLen(Tempstr))
{
StripTrailingWhitespace(Tempstr);
ptr=GetToken(Tempstr,":",&Name,0);

if (strcasecmp(Name,"Content-Disposition")==0)
{
	ptr=GetNameValuePair(ptr,";","=",&Name,&Value);
	while (ptr)
	{
		StripLeadingWhitespace(Name);
		StripTrailingWhitespace(Name);
		StripLeadingWhitespace(Value);
		StripTrailingWhitespace(Value);
	if (strcasecmp(Name,"name")==0) 
	{
		*Field=CopyStr(*Field,Value);
		result=TRUE;
	}

	if (strcasecmp(Name,"filename")==0) 
	{
		*FileName=CopyStr(*FileName,Value);
		result=TRUE;
	}
	ptr=GetNameValuePair(ptr,";","=",&Name,&Value);
	}
}
Tempstr=STREAMReadLine(Tempstr,S);
StripTrailingWhitespace(Tempstr);
}

DestroyString(Tempstr);
DestroyString(Name);
DestroyString(Value);

return(result);
}


int MultipartReadFile(STREAM *S,char *FName,char *Boundary, int BoundaryLen)
{
char *Tempstr=NULL, *ptr;
int result, RetVal=FALSE;
STREAM *FOut=NULL;
off_t fsize;

FOut=STREAMOpenFile(FName,SF_CREAT | SF_TRUNC | SF_WRONLY);
if (FOut)
{
	Tempstr=SetStrLen(Tempstr,4096);
	result=STREAMReadBytesToTerm(S, Tempstr, 4096, '\n');
	while (result > -1)
	{
		if ( (result < (BoundaryLen + 6)) && (strncmp(Tempstr,Boundary,BoundaryLen)==0))
		{
			//As we read to a '\n' we may have left its '\r' partner attached to
			//the end of the data
			ptr=Tempstr+result-2;
			if (strcmp(ptr,"\r\n")==0) result-=2;
			if ((result >= BoundaryLen) && (strncmp(Tempstr+BoundaryLen,"--\r\n",4)==0)) 
			{
				//must remove '\r\n' from end of file (it's the start of the boundary)
				RetVal=UPLOAD_DONE;
			}
			break;
		}
		else if (FOut) STREAMWriteBytes(FOut,Tempstr,result);
		result=STREAMReadBytesToTerm(S, Tempstr, 4096, '\n');
	}

//If we read to a boundary then there will always be a \r\n on the end of the file, 
fsize=(off_t) STREAMTell(FOut);
if (fsize > 0) ftruncate(FOut->out_fd,fsize-2);

STREAMClose(FOut);
}

if (result==-1) RetVal=UPLOAD_DONE;

DestroyString(Tempstr);

return(RetVal);
}


void UploadMultipartPost(STREAM *S, HTTPSession *Session)
{
char *Tempstr=NULL, *Name=NULL, *FileName=NULL, *QName=NULL, *QValue=NULL;
char *Boundary;
int blen=0;

if (! (Session->Flags & SESSION_UPLOAD)) return;
Boundary=MCopyStr(Boundary,"--",Session->ContentBoundary, NULL);
blen=StrLen(Boundary);

LogToFile(Settings.LogPath,"HANDLE UPLOAD: %s %s %d",Session->URL, Boundary, Session->ContentSize);
Tempstr=STREAMReadLine(Tempstr,S);
while (Tempstr)
{
	StripTrailingWhitespace(Tempstr);
	if ((blen > 0) && (strncmp(Tempstr,Boundary,blen)==0)) 
	{
		//Check for end boundary
		if (strcmp(Tempstr+blen,"--")==0) break;

		if (UploadReadMultipartHeaders(S, &Name, &FileName))
		{
			if (StrLen(FileName) > 0)
			{
			Tempstr=MCopyStr(Tempstr,Session->Path,"/",FileName,NULL);
			if (MultipartReadFile(S,Tempstr,Boundary, blen)==UPLOAD_DONE) break;
			else 
			{
				//we must have found a content boundary in ReadMultipartHeaders,
				//so don't read another line, deal with the content boundary
				Tempstr=CopyStr(Tempstr,Boundary);
				continue;
			}
			}
			else if (StrLen(Name) > 0)
			{
				Tempstr=STREAMReadLine(Tempstr,S);
				StripTrailingWhitespace(Tempstr);
				QName=HTTPQuote(QName,Name);
				QValue=HTTPQuote(QValue,Tempstr);
				Session->Arguments=MCatStr(Session->Arguments,"&",QName,"=",QValue,NULL);
			}
		}
	}
	
Tempstr=STREAMReadLine(Tempstr,S);
}

//Protect final argument for broken CGI implementations that read all
//data without regard to 'ContentSize'
Session->Arguments=CatStr(Session->Arguments,"&");
Session->ContentSize=StrLen(Session->Arguments);
if (Session->ContentSize > 0) Session->ContentType=CopyStr(Session->ContentType,"application/x-www-form-urlencoded");

DestroyString(Boundary);
DestroyString(FileName);
DestroyString(Tempstr);
DestroyString(Name);
DestroyString(QName);
DestroyString(QValue);
}


void UploadSelectPage(STREAM *S,HTTPSession *Session,char *Path)
{
char *HTML=NULL, *Tempstr=NULL;
int i;

  HTML=MCopyStr(HTML,"<html>\r\n<head><title>Upload files to: ",Session->URL,"</title></head>\r\n<body><form method=\"post\" enctype=\"multipart/form-data\" action=\"",Session->URL,"\">\r\n",NULL);

  HTML=MCatStr(HTML,"<p align=center>Upload files to: ",Session->URL,"</p>\r\n",NULL);
	HTML=CatStr(HTML,"<table align=center border=0><tr><th bgcolor=#AAAAFF>Select files for upload</th></tr>\r\n");
	for (i=0; i < 10; i++)
	{
  Tempstr=FormatStr(Tempstr,"<tr><td><input type=file name=uploadfile:%d></td></tr>\r\n",i);
  HTML=CatStr(HTML,Tempstr);
	}
	HTML=MCatStr(HTML,"<tr><td><input type=submit value=Upload></td></tr></table>\r\n",NULL);

  HTML=MCatStr(HTML,"</form></body></html>\r\n",NULL);

	HTTPServerSendResponse(S, Session, "200 OK","text/html",HTML);

DestroyString(HTML);
DestroyString(Tempstr);
}


