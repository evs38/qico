/**********************************************************
 * File: queue.c
 * Created at Thu Jul 15 16:14:46 1999 by pk // aaz@ruxy.org.ru
 * Queue operations 
 * $Id: queue.c,v 1.1.1.1 2000/07/18 12:37:21 lev Exp $
 **********************************************************/
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include "ftn.h"
#include "qipc.h"
#include "qconf.h"

qitem_t *q_queue=NULL;

#define _Z(x) ((qitem_t *)x)
int q_cmp(const void *q1, const void *q2)
{
	if(_Z(q1)->addr.z!=_Z(q2)->addr.z) return _Z(q1)->addr.z-_Z(q2)->addr.z;
	if(_Z(q1)->addr.n!=_Z(q2)->addr.n) return _Z(q1)->addr.n-_Z(q2)->addr.n;
	if(_Z(q1)->addr.f!=_Z(q2)->addr.f) return _Z(q1)->addr.f-_Z(q2)->addr.f;
	return _Z(q1)->addr.p-_Z(q2)->addr.p;
}

qitem_t *q_find(ftnaddr_t *fa)
{
	qitem_t *i;int r=1;
	for(i=q_queue;i && (r=q_cmp(&i->addr, fa))<0;i=i->next);
	return r?NULL:i;
}

qitem_t *q_add(ftnaddr_t *fa)
{
	qitem_t **i, *q;
	int r=1;
	for(i=&q_queue;*i && (r=q_cmp(&(*i)->addr, fa))<0;i=&((*i)->next));
	if(r==0) return *i;
	q=calloc(1,sizeof(qitem_t));
	q->next=*i;*i=q;
	ADDRCPY(q->addr, (*fa));
	return q;
}

void q_recountflo(char *name, off_t *size, time_t *mtime)
{
	FILE *f;
	struct stat sb;
	char s[MAX_STRING], *p;
	off_t total=0;

	if(!stat(name, &sb)) {
		if(sb.st_mtime!=*mtime) {
			*mtime=sb.st_mtime;
			f=fopen(name, "rt");
			if(f) {
				while(fgets(s, MAX_STRING-1, f)) {
					if(*s=='~') continue; 
					p=strrchr(s, '\r');if(p) *p=0;
					p=strrchr(s, '\n');if(p) *p=0;
					p=s;if(*p=='^'||*p=='#') p++;
					if(!stat(p, &sb)) total+=sb.st_size;
				}
				fclose(f);
			} else log("can't open %s: %s", name, strerror(errno));
			*size=total;
		}
	} else {
		*mtime=0;
		*size=0;
	}
}

void q_each(char *fname, ftnaddr_t *fa, int type, int flavor)
{
	qitem_t *q;
	struct stat sb;

	q=q_add(fa);q->touched=1;
	q->what|=type;
	switch(type) {
	case T_REQ:
		if(!stat(fname, &sb)) q->reqs=sb.st_size;
		break;
	case T_NETMAIL:
		if(!stat(fname, &sb)) q->pkts=sb.st_size;
		break;
	case T_ARCMAIL:
		q_recountflo(fname, &q->sizes[flavor-1], &q->times[flavor-1]);
		break;
	}
	switch(flavor) {
	case F_HOLD: q->flv|=Q_HOLD;break;
	case F_CRSH: q->flv|=Q_IMM|Q_NORM;break;
	default: q->flv|=Q_NORM;
	}
}

off_t q_sum(qitem_t *q)
{
	int i;
	off_t total=0;
	for(i=0;i<6;i++) if(q->times[i]) total+=q->sizes[i];
	return total;
}

void q_recountbox(char *name, off_t *size, time_t *mtime)
{
	DIR *d;struct dirent *de;struct stat sb;
	char *p;
	off_t total=0;
	
	if(!stat(name, &sb)) {
		if(sb.st_mtime!=*mtime) {
			*mtime=sb.st_mtime;
			d=opendir(name);
			if(d) {
				while((de=readdir(d))) {
					p=malloc(strlen(name)+2+strlen(de->d_name));
					sprintf(p,"%s/%s", name, de->d_name);
					if(!stat(p,&sb)&&S_ISREG(sb.st_mode)) 
						total+=sb.st_size;
					free(p);
				}
				closedir(d);
			} else log("can't open %s: %s", name, strerror(errno));
			*size=total;
		}
	} else {
		*mtime=0;
		*size=0;
	}
}	

void rescan_boxes()
{
	faslist_t *i;
	qitem_t *q;
	DIR *d;struct dirent *de;
	ftnaddr_t a;char *p;

	for(i=cfgfasl(CFG_FILEBOX);i;i=i->next) {
		q=q_add(&i->addr);
		q_recountbox(i->str, &q->sizes[4], &q->times[4]);
		if(q->sizes[4]!=0) {
			q->touched=1;
			q->flv|=Q_HOLD;
			q->what|=T_ARCMAIL;
		}
	}

	if(cfgs(CFG_LONGBOXPATH)) {
		d=opendir(ccs);
		if(d) {
			while((de=readdir(d))) 
				if(sscanf(de->d_name, "%hd.%hd.%hd.%hd",
						  &a.z, &a.n, &a.f, &a.p)==4) {
					p=malloc(strlen(ccs)+2+strlen(de->d_name));
					sprintf(p,"%s/%s", ccs, de->d_name);
					q=q_add(&a);
					q_recountbox(p, &q->sizes[5], &q->times[5]); 
					if(q->sizes[4]!=0) {
						q->touched=1;
						q->flv|=Q_HOLD;
						q->what|=T_ARCMAIL;
					}
					free(p);
			}
			closedir(d);
		} else log("can't open %s: %s", ccs, strerror(errno));
	}
}

int q_rescan(qitem_t **curr)
{
	sts_t sts;
	qitem_t *q, **p;

	for(q=q_queue;q;q=q->next) {
		q->what=0;q->flv&=Q_DIAL;q->touched=0;
	}
	
	qqreset();
	if(!bso_rescan(q_each)) return 0;
	rescan_boxes();
	p=&q_queue;
	while((q=*p)) {
		if(!q->touched) {
			*p=q->next;if(q==*curr) *curr=*p;free(q);
		} else {
			bso_getstatus(&q->addr, &sts);
			q->flv|=sts.flags;q->try=sts.try;
			if(sts.htime>time(NULL))
				q->onhold=sts.htime;
			else
				q->flv&=~Q_ANYWAIT;
			qpqueue(&q->addr,q->pkts,q_sum(q)+q->reqs,q->try,q->flv);
			p=&((*p)->next);
		}
	}
	if(!*curr) *curr=q_queue;
	return 1;
}

