/**********************************************************
 * File: session.c
 * Created at Sun Jul 18 18:28:57 1999 by pk // aaz@ruxy.org.ru
 * session
 * $Id: session.c,v 1.2.2.2 2000/12/12 11:27:59 lev Exp $
 **********************************************************/
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include "defs.h"
#include "ftn.h"
#include "zmodem.h"
#include "mailer.h"
#include "qconf.h"
#include "qipc.h"
#include "globals.h"
#include "hydra.h"
#include "ver.h"

void addflist(flist_t **fl, char *loc, char *rem, char kill,
			   off_t off, FILE *lo)
{
	flist_t **t, *q;
	int type;

	type=whattype(rem);
	switch(type) {
	case IS_REQ:
		if(rnode->options&(O_HRQ|O_NRQ)) return;
		break;
	case IS_PKT:
		if(rnode->options&O_HAT) return;
		break;
	default:
		if(rnode->options&(O_HXT|O_HAT)) return;
		break;
	}
	for(t=fl;*t && (*t)->type<=type;t=&((*t)->next));
	q=(flist_t *)malloc(sizeof(flist_t));
	q->next=*t;*t=q;
	q->kill=kill;q->loff=off;
	if((rnode->options&O_FNC) && rem) {
	    q->sendas=strdup(fnc(rem));
	    sfree(rem);
	} else q->sendas=rem;
	q->lo=lo;q->tosend=loc;
	q->type=type;
}


void floflist(flist_t **fl, char *flon)
{
	FILE *f;
	off_t off;
	char *p,str[MAX_PATH+1],*l,*m,*map=cfgs(CFG_MAPOUT),*fp,*fn;
	slist_t *i;
	struct stat sb;
	int len;

	if(!stat(flon, &sb)) if((f=fopen(flon, "r+b"))) {
		off=ftell(f);
		while(fgets(str, MAX_PATH, f)) {
			p=strrchr(str, '\r');if(!p) p=strrchr(str, '\n');
			if(p) *p=0;
			if(!str[0] && str[0]=='~') continue;
			p=str;
			switch(*p) {
			case '~': break;
			case '^': /* kill */
			case '#': /* trunc */
				p++;
			default:
				for(i=cfgsl(CFG_MAPPATH);i;i=i->next) {
					for(l=i->str;*l && *l!=' ';l++);
					for(m=l;*m==' ';m++);
					len=l-i->str;
					if(!*l||!*m) log("bad mapping '%s'!", i->str);
					else if(!strncasecmp(i->str,p,len)) {
						memmove(p+strlen(m),p+len,strlen(p+len)+1);
						memcpy(p,m,strlen(m));
					}
				}
				if(map && strchr(map, 'S')) strtr(p,'\\','/');
				fp=strdup(p);l=strrchr(fp, '/');if(l) l++;else l=fp;
				if(map && strchr(map, 'U')) strupr(l);
				if(map && strchr(map, 'L')) strlwr(l);
				
				fn=strrchr(p, '/');if(fn) fn++;else fn=p;
				mapname(fn, map);
				
				addflist(fl, fp, strdup(fn), str[0], off, f);
				
				if(!stat(fp,&sb)) {
					totalf+=sb.st_size;totaln++;
				}
			}		
			off=ftell(f);
		}
		addflist(fl, strdup(flon), NULL, '^', -1, f);
	}
}

int boxflist(flist_t **fl, char *path)
{
	DIR *d;char *p;struct dirent *de;struct stat sb;
	
	d=opendir(path);
	if(!d) return 0;
	else {
		while((de=readdir(d))) {
			p=malloc(strlen(path)+2+strlen(de->d_name));
			sprintf(p,"%s/%s", path, de->d_name);
			if(!stat(p,&sb)&&S_ISREG(sb.st_mode)) {
				addflist(fl, p,
						 strdup(mapname(de->d_name, cfgs(CFG_MAPOUT))),
						 '^', 0, NULL);
				totalf+=sb.st_size;totaln++;
			} else sfree(p);
		}
		closedir(d);
	}
	return 1;
}

void makeflist(flist_t **fl, ftnaddr_t *fa)
{
	int fls[]={F_IMM, F_CRSH, F_DIR, F_NORM, F_HOLD}, i;
	char str[MAX_PATH];
	struct stat sb;
	faslist_t *j;

#ifdef S_DEBUG	
	log("mkflist %s", ftnaddrtoa(fa));
#endif	
	for(i=0;i<5;i++)
		if(!stat(bso_pktn(fa, fls[i]), &sb)) {
			sprintf(str, "%08lx.pkt", sequencer());
			addflist(fl, strdup(bso_tmp), strdup(str), '^', 0, NULL);
			totalm+=sb.st_size;totaln++;
		}
	
	if(!stat(bso_reqn(fa), &sb)) {
		sprintf(str, "%04x%04x.req", fa->n, fa->f);
		addflist(fl, strdup(bso_tmp), strdup(str), ' ', 0, NULL);
		totalf+=sb.st_size;totaln++;
	}
	
	for(i=0;i<5;i++) floflist(fl, bso_flon(fa, fls[i]));

	for(j=cfgfasl(CFG_FILEBOX);j;j=j->next) 
		if(ADDRCMP((*fa),j->addr)) {
			if(!boxflist(fl, j->str))
				log("can't open filebox '%s'!", j->str);
			break;
		}

	if(cfgs(CFG_LONGBOXPATH)) {
		sprintf(str, "%s/%d.%d.%d.%d", ccs, fa->z, fa->n, fa->f, fa->p); 
		boxflist(fl, str);
	}
}

void flexecute(flist_t *fl)
{
	char cmt='~', str[MAX_STRING];
	FILE *f;int rem;

	if(fl->lo) {
		if(fl->loff<0) {
			fseek(fl->lo, 0L, SEEK_SET);
			rem=0;
			while(fgets(str, MAX_STRING, fl->lo)) 
				if(*str!='~' && *str!='\n' && *str!='\r') rem++;
			fclose(fl->lo);fl->lo=NULL;
			if(!rem) lunlink(fl->tosend);
		} else if(fl->sendas) {
			switch(fl->kill) {
			case '^':
				lunlink(fl->tosend);break;
			case '#':
				f=fopen(fl->tosend, "w");
				if(f) fclose(f);
				else log("can't truncate %s!", fl->tosend);
				break;
			}
			fseek(fl->lo, fl->loff, SEEK_SET);
			fwrite(&cmt, 1, 1, fl->lo);
			sfree(fl->sendas);
		}
 	} else if(fl->sendas) {
		switch(fl->kill) {
		case '^':
			lunlink(fl->tosend);break;
		case '#':
			f=fopen(fl->tosend, "w");
			if(f) fclose(f);
			else log("can't truncate %s!", fl->tosend);
			break;
		}
		sfree(fl->sendas);
	}
}

void flkill(flist_t **l, int rc)
{
	flist_t *t;
	while(*l) {
		if((*l)->lo && (*l)->loff<0) {
			fseek((*l)->lo, 0L, SEEK_END);
			fclose((*l)->lo);
		}
		if((*l)->type==IS_REQ && rc && !(*l)->sendas) lunlink((*l)->tosend);
		sfree((*l)->sendas);
		sfree((*l)->tosend);
		t=(*l)->next;
		sfree(*l);*l=t;
	}	
}

int receivecb(char *fn)
{
	char *p=strrchr(fn,'.');
	if(!p) return 0;
	if(!strcasecmp(p,".req") && cfgs(CFG_EXTRP)) {
		char s[MAX_PATH], priv;
		FILE *f, *g;
		ftnaddr_t *ma=akamatch(&rnode->addrs->addr, cfgal(CFG_ADDRESS));

		priv='a';
		if(rnode->options&O_LST) priv='l';
		if(rnode->options&O_PWD) priv='p';

		sprintf(s,"/tmp/qreq.%04x",getpid());
		f=fopen(fn,"rt");
		if(!f) {log("can't open '%s' for reading",s);return 0;}
		g=fopen(s,"wt");
		if(!g) {log("can't open '%s' for writing",s);return 1;}
		while(fgets(s,MAX_PATH-1,f)) {
			p=s+strlen(s)-1;
			while(*p=='\r' || *p=='\n') *p--=0;
#ifdef S_DEBUG
			log("requested '%s'", s);
#endif
			fputs(s,g);fputc('\n',g);
		}
		fclose(f);fclose(g);
		sprintf(s, "%s -wazoo -%c -s%d %s /tmp/qreq.%04x /tmp/qfls.%04x /tmp/qrep.%04x",
				cfgs(CFG_EXTRP), priv, rnode->realspeed,
				ftnaddrtoa(&rnode->addrs->addr), getpid(),getpid(),getpid());
		log("exec '%s' returned rc=%d", s,
			execsh(s));
		sprintf(s,"/tmp/qreq.%04x",getpid());lunlink(s);
		sprintf(s,"/tmp/qfls.%04x",getpid());f=fopen(s,"rt");
		if(!f) {log("can't open '%s' for reading",s);return 1;}
		while(fgets(s,MAX_PATH-1,f)) {
			p=s+strlen(s)-1;
			while(*p=='\r' || *p=='\n') *p--=0;
			p=strrchr(s,' ');
			if(p) *p++=0;else p=s;
#ifdef S_DEBUG
			log("sending '%s' as '%s'", s, p);
#endif
			addflist(&fl, strdup(s), strdup((p!=s)?p:basename(s)), ' ',0,NULL);
			got_req=1;
		}
		fclose(f);sprintf(s,"/tmp/qfls.%04x",getpid());lunlink(s);
		sprintf(s,"/tmp/qrep.%04x",getpid());f=fopen(s,"rt");
		if(!f) {log("can't open '%s' for reading",s);return 1;}
		sprintf(s,"/tmp/qpkt.%04x",getpid());
		g=openpktmsg(ma, &rnode->addrs->addr,
					 rnode->sysop,cfgs(CFG_FREQFROM),
					 cfgs(CFG_FREQSUBJ),rnode->pwd,s);
		if(!g) {log("can't open '%s' for writing",s);return 1;}
		while(fgets(s,MAX_PATH-1,f)) {
			p=s+strlen(s)-1;
			while(*p=='\r' || *p=='\n') *p--=0;
			fputs(s,g);fputc('\r',g);
		}
		sprintf(s, "%s-%s/%s",
			cfgs(CFG_PROGNAME) == NULL ? progname :	cfgs(CFG_PROGNAME),
			cfgs(CFG_VERSION)  == NULL ? version  :	cfgs(CFG_VERSION),
			cfgs(CFG_OSNAME)   == NULL ? osname   : cfgs(CFG_OSNAME));
		closepkt(g, ma, s, cfgs(CFG_STATION));
		sprintf(s,"/tmp/qrep.%04x",getpid());lunlink(s);
		sprintf(s,"/tmp/qpkt.%04x",getpid());p=strdup(s);
		sprintf(s,"%08lx.pkt", sequencer());
		addflist(&fl, p, strdup(s), '^',0,NULL);
		return 1;
	}
	return 0;
}

int wazoosend(int zap)
{
	flist_t *l;
	int rc;
	unsigned long total=totalf+totalm;

	log("wazoo send");sline("Init zsend...");
	rc=zmodem_sendinit(zap);
	sendf.cps=1;sendf.allf=totaln;sendf.ttot=totalf;
 	if(!rc) for(l=fl;l;l=l->next) {
		if(l->sendas) {
			rc=zmodem_sendfile(l->tosend, l->sendas, &total, &totaln);
			if(rc<0) break;
			if(!rc || rc==ZSKIP) {
				if(l->type==IS_REQ) was_req=1;
				flexecute(l);
			}
		} else flexecute(l);
	}

	sline("Done zsend...");
	rc=zmodem_senddone();
	qpreset(1);
	if(rc<0) return 1;
	return 0;
}

int wazoorecv()
{
	int rc;
	log("wazoo receive");
	rc=zmodem_receive(cfgs(CFG_INBOUND));
	qpreset(0);
	if(rc==RCDO || rc==ERROR) return 1;
	return 0;
}

int hydra(int mode, int hmod, int rh1)
{
	flist_t *l;
	int rc=XFER_OK;

	sline("Hydra-%dk session", hmod*2);
	hydra_init(HOPT_XONXOFF|HOPT_TELENET, mode, hmod);
	for(l=fl;l;l=l->next) 
		if(l->sendas) {
			if(l->type==IS_REQ || !rh1) {
				rc=hydra_file(l->tosend, l->sendas);
				if(rc==XFER_ABORT) break;
				if(rc==XFER_OK || rc==XFER_SKIP) flexecute(l);
			}
		} else if(!rh1) flexecute(l);
	if(rc==XFER_ABORT) {
		hydra_deinit();
		return 1;
	}
	rc=hydra_file(NULL, NULL);

	for(l=fl;l;l=l->next) 
		if(l->sendas) {
			rc=hydra_file(l->tosend, l->sendas);
			if(rc==XFER_ABORT) break;
			if(rc==XFER_OK || rc==XFER_SKIP) flexecute(l);
		} else flexecute(l);
	if(rc==XFER_ABORT) {
		hydra_deinit();
		return 1;
	}
	rc=hydra_file(NULL, NULL);
	hydra_deinit();
	return rc==XFER_ABORT; 	
}

#define SIZES(x) (((x)<1024)?(x):(((x)<1048576)?((x)/1024):((x)/1048576)))
#define SIZEC(x) (((x)<1024)?'b':(((x)<1048576)?'k':'M'))

void log_rinfo(ninfo_t *e)
{
	falist_t *i;
	struct tm *t;
	char s[MAX_STRING]={0};
	int k=0;

	if((i=e->addrs)) { log("address: %s", ftnaddrtoa(&i->addr));i=i->next; }
	for(;i;i=i->next) {
		if(k) strcat(s, " ");strcat(s, ftnaddrtoa(&i->addr));
		k++;if(k==2) { log("    aka: %s", s);k=0;s[0]=0; }
	}
	if(k) log("    aka: %s", s);
	log(" system: %s", e->name);
	log("   from: %s", e->place);
	log("  sysop: %s", e->sysop);
	log("  phone: %s", e->phone);
	log("  flags: [%d] %s", e->speed, e->flags);
	log(" mailer: %s", e->mailer);
	t=gmtime(&e->time);
	log("   time: %02d:%02d:%02d, %s",
		t->tm_hour, t->tm_min, t->tm_sec,
		e->wtime ? e->wtime : "unknown");
	if(e->holded && !e->files && !e->netmail)
		log(" for us: %d%c on hold", SIZES(e->holded), SIZEC(e->holded));
	else
		log(" for us: %d%c mail; %d%c files",
			SIZES(e->netmail), SIZEC(e->netmail),
			SIZES(e->files), SIZEC(e->files));
}

int emsisession(int mode, ftnaddr_t *calladdr, int speed)
{
	int rc, emsi_lo=0, proto;
	unsigned long nfiles;
	char *mydat, *t, pr[2];
	falist_t *pp;
	qitem_t *q;

	was_req=0;got_req=0;receive_callback=receivecb;
	totaln=0;totalf=0;totalm=0;emsi_lo=0;
	if(mode) {
		log("starting outbound EMSI session");
		q=q_find(calladdr);
		if(q) {
			totalm=q->pkts;
			totalf=q_sum(q)+q->reqs;
		}
		mydat=emsi_makedat(calladdr, totalm, totalf, O_PUA,
						   cfgs(CFG_PROTORDER), NULL, 1);
		rc=emsi_send(mode, mydat);sfree(mydat);
		if(rc<0) return S_REDIAL;
		rc=emsi_recv(mode, rnode);
		if(rc<0) return S_REDIAL;
	} else {
		rc=emsi_recv(mode, rnode);
		if(rc<0) {
			log("unable to establish EMSI session");
			return S_REDIAL;
		}
		log("starting inbound EMSI session");
	}

	if(mode)
		title("Outbound session %s", ftnaddrtoa(&rnode->addrs->addr));
	else {
		if((t=getenv("CALLER_ID")) && strcasecmp(t,"none")) 
			title("Inbound session %s (CID %s)",
				  ftnaddrtoa(&rnode->addrs->addr), t);
		else
			title("Inbound session %s",
				  ftnaddrtoa(&rnode->addrs->addr));
	}			
	log_rinfo(rnode);
	for(pp=rnode->addrs;pp;pp=pp->next)
		bso_locknode(&pp->addr);
	if(mode) {
		if(!has_addr(calladdr, rnode->addrs)) {
			log("remote isn't %s!", ftnaddrtoa(calladdr));
			return S_UNDIAL;
		}
		flkill(&fl, 0);totalf=0;totalm=0;
		for(pp=rnode->addrs;pp;pp=pp->next) 
			makeflist(&fl, &pp->addr);
		if(strlen(rnode->pwd)) rnode->options|=O_PWD;
	} else {
		for(pp=cfgal(CFG_ADDRESS);pp;pp=pp->next)
			if(has_addr(&pp->addr, rnode->addrs)) {
				log("remote also has %s!", ftnaddrtoa(&pp->addr));
				return S_UNDIAL;
			}		
		nfiles=0;rc=0;
		for(pp=rnode->addrs;pp;pp=pp->next) {
			t=findpwd(&pp->addr);
			if(!t || !strcasecmp(rnode->pwd, t)) {
				makeflist(&fl, &pp->addr);
				if(t) rnode->options|=O_PWD;
			} else {
				log("password not matched for %s",ftnaddrtoa(&pp->addr));
				log("  (got '%s' instead of '%s')", rnode->pwd, t);
				rc=1;
			}
		}

		if(rc) {
			emsi_lo|=O_BAD;
			rnode->options|=O_BAD;
		}
		if(!cfgs(CFG_FREQTIME)) 
			emsi_lo|=O_NRQ;
		if(ccs && !checktimegaps(ccs))
			emsi_lo|=O_HRQ;
		if(checktimegaps(cfgs(CFG_MAILONLY)) ||
		   checktimegaps(cfgs(CFG_ZMH))) 
			emsi_lo|=O_HXT|O_HRQ;

		pr[1]=0;pr[0]=0;
		for(t=cfgs(CFG_PROTORDER);*t;t++) {
			if(*t=='8' && rnode->options&P_HYDRA8)
				{pr[0]='8';emsi_lo|=P_HYDRA8;break;}
			if(*t=='6' && rnode->options&P_HYDRA16)
				{pr[0]='6';emsi_lo|=P_HYDRA16;break;}
			if(*t=='H' && rnode->options&P_HYDRA)
				{pr[0]='H';emsi_lo|=P_HYDRA;break;}
			if(*t=='J' && rnode->options&P_JANUS)
				{pr[0]='J';emsi_lo|=P_JANUS;break;}
			if(*t=='Z' && rnode->options&P_ZEDZAP)
				{pr[0]='Z';emsi_lo|=P_ZEDZAP;break;}
			if(*t=='1' && rnode->options&P_ZMODEM)
				{pr[0]='1';emsi_lo|=P_ZMODEM;break;}
		}
		if(!pr[0]) emsi_lo|=P_NCP;
		mydat=emsi_makedat(&rnode->addrs->addr, totalm, totalf, emsi_lo,
						   pr, NULL, !(emsi_lo&O_BAD));
		rc=emsi_send(0, mydat);sfree(mydat);
		if(rc<0) {
			flkill(&fl,0);
			return S_REDIAL;
		}
	}
	log("we have: %d%c mail; %d%c files",
		SIZES(totalm), SIZEC(totalm),
		SIZES(totalf), SIZEC(totalf));

	rnode->starttime=time(NULL);
	if(cfgi(CFG_MAXSESSION)) alarm(cci*60);
	
	if(is_listed(&rnode->addrs->addr, cfgs(CFG_NLPATH)))
		rnode->options|=O_LST;
	qemsisend(rnode, rnode->options&O_PWD, rnode->options&O_LST);
	qpreset(0);qpreset(1);

	proto=(mode?rnode->options:emsi_lo)&P_MASK;
	switch(proto) {
	case P_NCP: 
		log("no compatible protocols");
		flkill(&fl, 0);
		return S_UNDIAL;
	case P_ZMODEM:
		t="ZModem-1k";break;
	case P_ZEDZAP:
		t="ZedZap";break;
	case P_HYDRA8:
		t="Hydra-8k";break;
	case P_HYDRA16:
		t="Hydra-16k";break;
	case P_HYDRA:
		t="Hydra";break;
	case P_JANUS:
		t="Janus";break;
	case P_TCPP:
		t="IFCTCP";break;
	default:
		t="Unknown";		
	}
#ifdef S_DEBUG	
	log("emsopts: %s %x %x %x", t, rnode->options&P_MASK, rnode->options, emsi_lo);
#endif
	log("options: %s%s%s%s%s%s%s%s", t,
		(rnode->options&O_LST)?"/LST":"",
		(rnode->options&O_PWD)?"/PWD":"",
		(rnode->options&O_HXT)?"/MO":"",
		(rnode->options&O_HAT)?"/HAT":"",
		(rnode->options&O_HRQ)?"/HRQ":"",
		(rnode->options&O_NRQ)?"/NRQ":"",
		(rnode->options&O_FNC)?"/FNC":""
		);
	
	switch(proto) {
	case P_ZEDZAP:
	case P_ZMODEM:  
		recvf.cps=1;recvf.ttot=rnode->netmail+rnode->files;
		if(mode) {
			rc=wazoosend(proto&P_ZEDZAP);
			if(!rc) rc=wazoorecv();
			if(got_req && !rc) rc=wazoosend(proto&P_ZEDZAP);
		} else {
			rc=wazoorecv();
			if(rc) return S_REDIAL;
			rc=wazoosend(proto&P_ZEDZAP);
			if(was_req) rc=wazoorecv();
		}
		flkill(&fl, !rc);
		return rc?S_REDIAL:S_OK;
	case P_HYDRA:
	case P_HYDRA8:
	case P_HYDRA16:
		sendf.allf=totaln;sendf.ttot=totalf+totalm;
		recvf.ttot=rnode->netmail+rnode->files;
		switch(proto) {
		case P_HYDRA:   rc=1;break;
		case P_HYDRA8:  rc=4;break;
		case P_HYDRA16: rc=8;break;
		}
		rc=hydra(mode, rc, rnode->options&O_RH1);
		flkill(&fl, !rc);
		return rc?S_REDIAL:S_OK;
	case P_JANUS:
		return S_OK;
	}
	return S_OK;
}

void sessalarm(int sig)
{
	signal(SIGALRM, SIG_DFL);
	log("session limit of %d minutes is over",
		cfgi(CFG_MAXSESSION));
	tty_hangedup=1;
}

int session(int mode, int type, ftnaddr_t *calladdr, int speed)
{
	int rc;
	time_t sest;
	FILE *h;
	char s[MAX_STRING];
	falist_t *pp;

	rnode->realspeed=effbaud=speed;rnode->starttime=0;
	if(!mode) rnode->options|=O_INB;
	if(is_ip) rnode->options|=O_TCP;

	rc=cfgi(CFG_MINSPEED);
	if(rc && speed<rc) {
		log("connection speed is too low");
		return S_REDIAL;
	}
		
	bzero(&sendf, sizeof(sendf));
	bzero(&recvf, sizeof(recvf));
	signal(SIGALRM, sessalarm);
	signal(SIGTERM, tty_sighup);
	signal(SIGINT, tty_sighup);
	signal(SIGCHLD, SIG_DFL);

	switch(type) {
	case SESSION_AUTO:
		log("trying EMSI...");
		rc=emsi_init(mode);
		if(rc<0) {
			log("unable to establish EMSI session");
			return S_REDIAL;
		}
		rc=emsisession(mode, calladdr, speed);
		break;
	case SESSION_EMSI: 
		rc=emsisession(mode, calladdr, speed);
		break;
	default:
		log("unsupported session type! (%d)", type);
		return S_REDIAL;
	}
	for(pp=rnode->addrs;pp;pp=pp->next) bso_unlocknode(&pp->addr);
	if(rnode->options&O_NRQ || rnode->options&O_HRQ) rc|=S_HOLDR;
	if(rnode->options&O_HXT) rc|=S_HOLDX;
	if(rnode->options&O_HAT) rc|=S_HOLDA;
	signal(SIGALRM, SIG_DFL);
	sest=rnode->starttime?time(NULL)-rnode->starttime:0;
	log("total: %d:%02d:%02d online, %d%c sent, %d%c received",
		sest/3600,sest%3600/60,sest%60,
		SIZES(sendf.toff-sendf.soff), SIZEC(sendf.toff-sendf.soff),
		SIZES(recvf.toff-recvf.soff), SIZEC(recvf.toff-recvf.soff));
	log("session with %s %s [%s]",
		rnode->addrs?ftnaddrtoa(&rnode->addrs->addr):(calladdr?ftnaddrtoa(calladdr):"unknown"),
		((rc&S_MASK)==S_OK)?"successful":"failed", M_STAT);
	if(rnode->starttime && cfgs(CFG_HISTORY)) {
		h=fopen(ccs,"at");
		if(!h) log("can't open %s for writing!",ccs);
		else {
			fprintf(h,"%s,%ld,%ld,%s,%s%s%c%d,%d,%d\n",
					rnode->tty,rnode->starttime, sest,
					rnode->addrs?ftnaddrtoa(&rnode->addrs->addr):(calladdr?ftnaddrtoa(calladdr):"unknown"),
					(rnode->options&O_PWD)?"P":"",(rnode->options&O_LST)?"L":"",
					(rnode->options&O_INB)?'I':'O',((rc&S_MASK)==S_OK)?1:0,
					sendf.toff-sendf.stot, recvf.toff-recvf.stot);
			fclose(h);
		}
	}
	sprintf(s,"/tmp/qpkt.%04x",getpid());
	if(fexist(s)) lunlink(s);
	if(cfgs(CFG_AFTERSESSION)) {
		log("starting %s %s %c %d",
			cfgs(CFG_AFTERSESSION),
			rnode->addrs?ftnaddrtoa(&rnode->addrs->addr):(calladdr?ftnaddrtoa(calladdr):"unknown"),
			(rnode->options&O_INB)?'I':'O',((rc&S_MASK)==S_OK)?1:0);
		execnowait(cfgs(CFG_AFTERSESSION),
				   rnode->addrs?ftnaddrtoa(&rnode->addrs->addr):(calladdr?ftnaddrtoa(calladdr):"unknown"),
				   (rnode->options&O_INB)?"I":"O",((rc&S_MASK)==S_OK)?"1":"0");
	}
	return rc;
}
