/**********************************************************
 * File: globals.c
 * Created at Thu Jul 15 16:11:48 1999 by pk // aaz@ruxy.org.ru
 * 
 * $Id: globals.c,v 1.1.1.1 2000/07/18 12:37:19 lev Exp $
 **********************************************************/
#include "qcconst.h"
#include "mailer.h"
char *osname="Unix";
int is_ip=0, do_rescan=0;
pfile_t sendf, recvf;
char ip_id[10];
ninfo_t *rnode=NULL;

ftnaddr_t DEFADDR={0,0,0,0};

int my_timezone;
unsigned long int totalf, totalm, totaln;
int was_req, got_req;
flist_t *fl=NULL;
