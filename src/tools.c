/**********************************************************
 * stuff
 * $Id: tools.c,v 1.8 2004/03/20 16:04:16 sisoft Exp $
 **********************************************************/
#include "headers.h"
#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif
#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif
#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif
#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif
#include "charset.h"

static unsigned long seq=0xFFFFFFFF;
static char *hexdigits="0123456789abcdef";

void strlwr(char *s)
{
	while(s&&*s){*s=tolower(*s);s++;}
}

void strupr(char *s)
{
	while(s&&*s){*s=toupper(*s);s++;}
}

void strtr(char *s,char a,char b)
{
	while(s&&*s) {
		if(*s==a)*s=b;
		s++;
	}
}

unsigned char todos(unsigned char c)
{
	if(cfgi(CFG_REMOTERECODE)&&c>=128)c=tab_koidos[c-128];
	return c;
}

unsigned char tokoi(unsigned char c)
{
	if(cfgi(CFG_REMOTERECODE)&&c>=128)c=tab_doskoi[c-128];
	return c;
}

void stodos(unsigned char *s)
{
	while(s&&*s){*s=todos(*s);s++;}
}

void stokoi(unsigned char *s)
{
	while(s&&*s){*s=tokoi(*s);s++;}
}

void chop(char *s,int n)
{
	char *p=strchr(s,0);
	while(p&&n--)*--p=0;
}

void strbin2hex(char *s,const unsigned char *bptr,size_t blen)
{
	while(blen--) {
		*s++=hexdigits[(*bptr>>4)&0x0f];
		*s++=hexdigits[(*bptr)&0x0f];
		bptr++;
	}
	*s=0;
}

int strhex2bin(unsigned char *binptr,const char *string)
{
	int i,val,len=strlen(string);
	unsigned char *dest=binptr;
	const char *p;
	for(i=0;2*i<len;i++) {
		if((p=strchr(hexdigits,tolower(*(string++))))) {
			val=(int)(p-hexdigits);
			if((p=strchr(hexdigits,tolower(*(string++))))) {
				val=val*16+(int)(p-hexdigits);
				*dest++=(unsigned char)(val&0xff);
			} else return 0;
		} else return 0;
	}
	return(dest-binptr);
}

size_t filesize(char *fname)
{
	size_t s;
	FILE *f=fopen(fname,"r");
	if(!f)return 0;
	fseek(f,0L,SEEK_END);
	s=ftell(f);fclose(f);
	return s;
}

int islocked(char *pidfn)
{
	FILE *f;
	long pid;
	if((f=fopen(pidfn,"rt"))) {
		fscanf(f,"%ld",&pid);
		fclose(f);
		if(kill((pid_t)pid,0)&&(errno==ESRCH))lunlink(pidfn);
		else return((int)pid);
	}
	return 0;
}

int lockpid(char *pidfn)
{
	int rc;
	FILE *f;
	char tmpname[MAX_PATH],*p;
	if(islocked(pidfn))return 0;
#ifndef LOCKSTYLE_OPEN
	xstrcpy(tmpname,pidfn,MAX_PATH);
	p=strrchr(tmpname,'/');
	if(!p)p=tmpname;
	snprintf(tmpname+(p-tmpname),MAX_PATH-(p-tmpname+1),"/QTEMP.%ld",(long)getpid());
	if(!(f=fopen(tmpname,"w")))return 0;
	fprintf(f,"%10ld\n",(long)getpid());
	fclose(f);
	rc=link(tmpname,pidfn);
	lunlink(tmpname);
	if(rc)return 0;
#else
	rc=open(pidfn,O_WRONLY|O_CREAT|O_EXCL,0644);
	if(rc<0)return 0;
	snprintf(tmpname,MAX_PATH,"%10ld\n",(long)getpid());
	write(rc,tmpname,strlen(tmpname));
	close(rc);
#endif
	return 1;
}

unsigned long sequencer()
{
	if(seq==0xFFFFFFFF||time(NULL)>seq)seq=time(NULL);
	    else seq++;
	return seq;
}

int touch(char *fn)
{
	FILE *f=fopen(fn,"a");
 	if(f) {
 		fclose(f);
 		return 1;
 	} else write_log("can't touch '%s': %s",fn,strerror(errno));
 	return 0;
}

int mkdirs(char *name)
{
	int rc=0;
	char *p=name+1,*q;
	while((q=strchr(p,'/'))) {
		*q=0;rc=mkdir(name,cfgi(CFG_DIRPERM));
		*q='/';p=q+1;
	}
	return rc;
}

void rmdirs(char *name)
{
	int rc=0;
	char *q=strrchr(name,'/'),*t;
	while(q&&q!=name&&!rc) {
		*q=0;rc=rmdir(name);
		t=strrchr(name,'/');
		*q='/';q=t;
	}
}

FILE *mdfopen(char *fn,char *pr)
{
	FILE *f;
	struct stat sb;
	int nf=(stat(fn,&sb))?1:0;
 	if((f=fopen(fn,pr))) {
		if(nf)fchmod(fileno(f),cfgi(CFG_DEFPERM));
		return f;
	}
	if(errno==ENOENT) {
		mkdirs(fn);
		f=fopen(fn,pr);
		if(f&&nf)fchmod(fileno(f),cfgi(CFG_DEFPERM));
		return f;
	}
	return NULL;
}

int fexist(char *s)
{
	struct stat sb;
	return(!stat(s,&sb)&&S_ISREG(sb.st_mode));
}

int dosallowin83(int c)
{
	static char dos_allow[]="!@#$%^&()~`'-_{}";
	if((c>='a'&&c <= 'z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
		strchr(dos_allow,c))return 1;
	return 0;
}

char *fnc(char *s)
{
	char *p,*q;
	unsigned int i=0;
	static char s8[13];
	if(!s)return NULL;
	if(!(p=strrchr(s,'/')))p=s; else s=p;
	while(*p&&*p!='.'&&i<8) {
		if(dosallowin83(*p))s8[i++]=tolower(*p);
		p++;
	}
	s8[i]=0;
	if(strstr(s,".tar.gz"))xstrcat(s8,".tgz",14);
	else if(strstr(s,".tar.bz2"))xstrcat(s8,".tb2",14);
	else if(strstr(s,".html"))xstrcat (s8,".htm",14);
	else if(strstr(s,".jpeg"))xstrcat (s8,".jpg",14);
	else if(strstr(s,".desc"))xstrcat (s8,".dsc",14);
	    else {
		if((p=strrchr(s,'.'))) {
			xstrcat(s8,".",14);
			q=p+4;i=strlen(s8);
			while(*p&&q>p) {
				if(dosallowin83(*p))s8[i++]=tolower(*p);
				p++;
			}
			s8[i]=0;
		}
	}
	return s8;
}

int lunlink(char *s)
{
	int rc=unlink(s);
	if(rc<0&&errno!=ENOENT)write_log("can't delete file %s: %s",s,strerror(errno));
	return rc;
}

int isdos83name(char *fn)
{
	int nl=0,el=0,ec=0,uc=0,lc=0,f=1;
	while(*fn) {
		if(!dosallowin83(*fn)&&(*fn!='.')) {
			f=0;
			break;
		}
	    	if(*fn=='.')ec++;
	    	    else {
			if(!ec)nl++;
			    else el++;
			if(isalpha((int)*fn)) {
				if(isupper((int)*fn))uc++;
				    else lc++;
			}
		}
	    	fn++;
	}
	return(f&&ec<2&&el<4&&nl<9&&(!lc||!uc));
}

#if defined(HAVE_STATFS) && defined(STATFS_HAVE_F_BAVAIL)
#define STAT_V_FS statfs
#else
#if defined(HAVE_STATVFS) && defined(STATVFS_HAVE_F_BAVAIL)
#define STAT_V_FS statvfs
#else
#undef STAT_V_FS
#endif
#endif
size_t getfreespace(const char *path)
{
#ifdef STAT_V_FS
	struct STAT_V_FS sfs;
	if(!STAT_V_FS(path,&sfs)) {
		if(sfs.f_bsize>=1024)return((sfs.f_bsize/1024L)*sfs.f_bavail);
		    else return(sfs.f_bavail/(1024L/sfs.f_bsize));
	} else write_log("can't statfs '%s'",path);
#endif
	return(~0L);
}

void to_dev_null()
{
	int fd;
	close(STDIN_FILENO);close(STDOUT_FILENO);close(STDERR_FILENO);
	fd=open(devnull,O_RDONLY);
	if(dup2(fd,STDIN_FILENO)!=STDIN_FILENO){write_log("reopening of stdin failed");exit(-1);}
	if(fd!=STDIN_FILENO)close(fd);
	fd=open(devnull,O_WRONLY|O_APPEND|O_CREAT,0600);
	if(dup2(fd,STDOUT_FILENO)!=STDOUT_FILENO){write_log("reopening of stdout failed");exit(-1);}
	if(fd!=STDOUT_FILENO)close(fd);
	fd=open(devnull,O_WRONLY|O_APPEND|O_CREAT,0600);
	if(dup2(fd,STDERR_FILENO)!=STDERR_FILENO){write_log("reopening of stderr failed");exit(-1);}
	if(fd!=STDERR_FILENO)close(fd);
}

int randper(int base,int diff)
{
	return base-diff+(int)(diff*2.0*rand()/(RAND_MAX+1.0));
}

#ifndef HAVE_SETPROCTITLE
extern char **environ;
static char *cmdstr=NULL;
static char *cmdstrend=NULL;

void setargspace(int argc,char **argv,char **envp)
{
	int i=0;
	cmdstr=argv[0];
	while(envp[i])i++;
	environ=xmalloc(sizeof(char*)*(i+1));
	i=0;
	while(envp[i]) {
		environ[i]=xstrdup(envp[i]);
		i++;
	}
	environ[i]=NULL;
	cmdstrend=argv[0]+strlen(argv[0]);
	for(i=1;i<argc;i++)if(cmdstrend+1==argv[i])cmdstrend=argv[i]+strlen(argv[i]);
	for(i=0;envp[i];i++)if(cmdstrend+1==envp[i])cmdstrend=envp[i]+strlen(envp[i]);
}

void setproctitle(char *str)
{
	char *p;
	if(!cmdstr)return;
	for(p=cmdstr;p<cmdstrend&&*str;p++,str++)*p=*str;
	while(p<cmdstrend)*p++=' ';
}
#endif