#include "FileProperties.h"
#include "MimeType.h"

#define USE_XATTR 1

#ifdef USE_XATTR
 #include <sys/types.h>
 #include <sys/xattr.h>
#endif


char *PhysicalProperties[]={"creationdate","CTime-secs", "getlastmodified","MTime-secs","getcontentlength","FileSize","getcontenttype","ContentType","executable","IsExecutable"};




int SaveFileXAttr(char * Path, ListNode *PropList)
{
ListNode *Curr;
char *Attr=NULL;
int result=FALSE;

#ifdef USE_XATTR
Curr=ListGetNext(PropList);
while (Curr)
{
	if (MatchTokenFromList(Curr->Tag,PhysicalProperties,0)==-1)
	{
		Attr=MCopyStr(Attr,"user.",Curr->Tag,NULL);
		if (setxattr(Path, Attr, (char *) Curr->Item, StrLen(Curr->Item), 0)==0) result=TRUE;	
	}
Curr=ListGetNext(Curr);
}
#endif

DestroyString(Attr);

return(result);
}


int LoadFileXAttr(char *Path, ListNode *PropList)
{
ListNode *Curr;
int result=FALSE, len;
char *AttrNamesList=NULL, *Attr=NULL, *Value=NULL;
char *ptr, *end;

#ifdef USE_XATTR

AttrNamesList=SetStrLen(AttrNamesList,BUFSIZ);
len=listxattr(Path, AttrNamesList, BUFSIZ);
if (len ==-1) return(FALSE);

end=AttrNamesList+len;

/* Loop through all EA names, displaying name + value */
for (ptr=AttrNamesList; ptr < end; ptr += strlen(ptr) + 1) 
{
	//paranoid check in case something goes wrong in our for loop
	if (ptr > end) break;

		Value=SetStrLen(Value,4096);
		len=getxattr(Path, ptr, Value, 4096);
		if (strncmp(ptr,"user.",5)==0) ptr+=5;

		SetTypedVar(PropList,ptr,Value,FILE_USER_VALUE);
}
#endif

DestroyString(Attr);
DestroyString(Value);
DestroyString(AttrNamesList);

return(result);
}



void LoadDirPropsFile(char *Dir, char *RequestedFile, ListNode *Props)
{
char *Tempstr=NULL, *Token=NULL, *FName=NULL, *ptr;
STREAM *S;

Tempstr=MCopyStr(Tempstr,Dir,"/.props",NULL);

S=STREAMOpenFile(Tempstr,O_CREAT | O_RDWR);
if (S)
{
	Tempstr=STREAMReadLine(Tempstr,S);
	while (Tempstr)
	{
		StripTrailingWhitespace(Tempstr);
		if (StrLen(Tempstr))
		{
			ptr=GetToken(Tempstr,"=",&Token,GETTOKEN_QUOTES);
			GetToken(Token,":",&FName,GETTOKEN_QUOTES);

			if ( (! StrLen(RequestedFile)) || (strcmp(RequestedFile,FName)==0)) SetTypedVar(Props,Token,ptr,FILE_USER_VALUE);
		}
		Tempstr=STREAMReadLine(Tempstr,S);
	}
STREAMClose(S);
}

DestroyString(Tempstr);
DestroyString(Token);
DestroyString(FName);
}


void SaveDirPropsFile(char *Dir, char *RequestedFile, ListNode *Props)
{
char *Tempstr=NULL, *Token=NULL, *ptr;
ListNode *Curr;
STREAM *S;

Tempstr=MCopyStr(Tempstr,Dir,"/.props",NULL);

S=STREAMOpenFile(Tempstr,O_CREAT | O_WRONLY | O_TRUNC);
if (S)
{
	Curr=ListGetNext(Props);
	while (Curr)
	{
		if (MatchTokenFromList(Curr->Tag,PhysicalProperties,0)==-1)
		{
		Tempstr=MCopyStr(Tempstr,RequestedFile,":",Curr->Tag,"=",Curr->Item,"\n",NULL);
		STREAMWriteLine(Tempstr,S);
		}
	Curr=ListGetNext(Curr);
	}
STREAMClose(S);
}

DestroyString(Tempstr);
DestroyString(Token);
}




//Real Properties are things like file size, mtime, etc, that are not strings defined
//by the user
int PropertiesLoadFromStream(char *FName, STREAM *S, ListNode *Vars)
{
char *Buffer=NULL;
int result;
TFileMagic *FM=NULL;
struct stat FileStat;

if (stat(FName,&FileStat)==-1) return(FILE_NOSUCH);



Buffer=FormatStr(Buffer,"%llu",(unsigned long long) FileStat.st_size);
SetVar(Vars,"FileSize",Buffer);
Buffer=FormatStr(Buffer,"%d",FileStat.st_ctime);
SetVar(Vars,"CTime-Secs",Buffer);
Buffer=FormatStr(Buffer,"%d",FileStat.st_mtime);
SetVar(Vars,"MTime-Secs",Buffer);


//if it's a directory, don't both doing any more examining
if (S_ISDIR(FileStat.st_mode)) 
{
	SetVar(Vars,"IsCollection","1");
	SetVar(Vars,"ContentType","Directory");
	DestroyString(Buffer);
	return(FILE_DIR);
}


if (! (FileStat.st_mode & S_IWUSR)) SetVar(Vars,"IsReadOnly","1");
if ((FileStat.st_mode & S_IXUSR)) SetVar(Vars,"IsExecutable","T");
else SetVar(Vars,"IsExecutable","F");


//This function can handle S being NULL
FM=GetFileMagicForFile(FName, S);
if (FM) 
{
	SetVar(Vars,"ContentType",FM->ContentType);
	// if 'S' open then ExamineContents WAS set
	if (S) 
	{
	if (FM->Flags & (FM_MEDIA_TAG | FM_IMAGE_TAG))
	{
		MediaReadDetails(S, Vars);
		STREAMSeek(S,0,SEEK_SET);
	}
	}
}


DestroyString(Buffer);

return(TRUE);
}



int LoadFileRealProperties(char *FName, int ExamineContents, ListNode *Vars)
{
STREAM *S=NULL;
int result;

if (ExamineContents) S=STREAMOpenFile(FName,O_RDONLY);

result=PropertiesLoadFromStream(FName, S, Vars);

STREAMClose(S);

return(result);
}



int LoadFileProperties(char *Path, ListNode *PropList)
{
char *Dir=NULL, *ptr;
int FType;

ptr=GetToken(Path,"/",&Dir,0);

if (! LoadFileXAttr(Path,PropList))
{
	LoadDirPropsFile(Dir, ptr, PropList);
}

FType=LoadFileRealProperties(Path, TRUE, PropList);

//Translate Some Props  to DAV names
SetVar(PropList,"creationdate",GetVar(PropList,"CTime-secs"));
SetVar(PropList,"getlastmodified",GetVar(PropList,"MTime-secs"));
SetVar(PropList,"getcontentlength",GetVar(PropList,"FileSize"));
SetVar(PropList,"getcontenttype",GetVar(PropList,"ContentType"));
SetVar(PropList,"executable",GetVar(PropList,"IsExecutable"));

return(FType);
}


int SaveFileProperties(char *Path, ListNode *PropList)
{
char *Dir=NULL, *ptr;
int FType;

ptr=GetToken(Path,"/",&Dir,0);

if (! SaveFileXAttr(Path,PropList))
{
	SaveDirPropsFile(Dir, ptr, PropList);
}

//Translate Some Props  to DAV names
/*
SetVar(PropList,"creationdate",GetVar(PropList,"CTime-secs"));
SetVar(PropList,"getlastmodified",GetVar(PropList,"MTime-secs"));
SetVar(PropList,"getcontentlength",GetVar(PropList,"FileSize"));
SetVar(PropList,"getcontenttype",GetVar(PropList,"ContentType"));
SetVar(PropList,"executable",GetVar(PropList,"IsExecutable"));
*/

return(FType);
}



void SetProperties(char *File, ListNode *Props)
{
char *Token=NULL, *Dir=NULL, *FName=NULL, *ptr;
ListNode *Curr, *FProps, *Node;

ptr=strrchr(File,'/');
if (ptr) 
{
	Dir=CopyStrLen(Dir,File,ptr-File);
	ptr++;
}
else 
{
	Dir=CopyStr(Dir,"/");
	ptr=File;
}

FName=CopyStr(FName,ptr);
FProps=ListCreate();
LoadFileProperties(File, FProps);

Curr=ListGetNext(Props);
while (Curr)
{
	SetVar(FProps,Curr->Tag,(char *) Curr->Item);
	LogToFile(Settings.LogPath,"SP: [%s] [%s]\n",Curr->Tag,Curr->Item);
	Curr->Item=CopyStr(Curr->Item,"HTTP/1.1 200 OK");
	Curr=ListGetNext(Curr);
}

SaveFileProperties(File,FProps);

ListDestroy(FProps,DestroyString);
DestroyString(Token);
DestroyString(Dir);
DestroyString(FName);
}


