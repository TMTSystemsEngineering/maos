/*
  Copyright 2009, 2010 Lianqi Wang <lianqiw@gmail.com> <lianqiw@tmt.org>
  
  This file is part of Multithreaded Adaptive Optics Simulator (MAOS).

  MAOS is free software: you can redistribute it and/or modify it under the
  terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version.

  MAOS is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with
  MAOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
   Contains routines to do job scheduling and monitoring.

   Change log: 

   2010-01-17: Removed communicate between
   scheduler. This is complicated and not reliable. It is up
   to the monitor to connect all the servers to get status
   information.
   
   2010-07-26: Split scheduler to scheduler_server and scheduler_client.

   socket programming guideline: 

   1) Use a socket for only read or write if persistent
   connection is desired to detect connection closure due to
   program crash or shutdown write on socket.

   2) Do not pass host id around, as it might be defined
   differently for the same machine.

   \todo Detect hyperthreading.
 */
#ifndef __CYGWIN__

#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h> 
#include <errno.h>
#include <arpa/inet.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <limits.h>
#include <string.h>
#include "config.h"
#include "misc.h"
#include "sockio.h"
#include "hashlittle.h"
#include "io.h"
#include "process.h"
#include "daemonize.h"
#include "scheduler_server.h"
#include "shm.h"
char** hosts;
int nhost;
static int all_done=0;
/*A linked list to store running process*/
RUN_T *running=NULL;//points to the begining
RUN_T *running_end=NULL;//points to the last to add.
/*A linked list to store ended process*/
RUN_T *runned=NULL;
RUN_T *runned_end=NULL;
static int nrun=0;
MONITOR_T *pmonitor=NULL;
static char *fnlog=NULL;
uint16_t PORT=0;
uint16_t PORTMON=0;
int hid=0;
static int scheduler_sock;
static double usage_cpu;
fd_set active_fd_set;

static RUN_T* running_add(int pid,int sock);
static RUN_T *running_get(int pid);
static RUN_T *running_get_wait(void);

static RUN_T *runned_get(int pid);
static void runned_remove(int pid);
static int runned_add(RUN_T *irun);
static void running_remove(int pid);
static void running_update(int pid, int status);
static RUN_T *running_get_by_sock(int sock);

int myhostid(const char *host){
    int i;
    for(i=0; i<nhost; i++){
	if(!strncasecmp(hosts[i],host,strlen(hosts[i])))
	    break;
    }
    if(i==nhost){
	i=-1;
    }
    return i;
}
//Initialize hosts and associate an id number
static __attribute__((constructor))void init(){
    init_path();//the constructor in proc.c may not have been called.
    char fn[PATH_MAX];
    snprintf(fn,PATH_MAX,"%s/.aos/jobs.log", HOME);
    fnlog=strdup(fn);
    snprintf(fn,PATH_MAX,"%s/.aos/port",HOME);
    PORT=0;
    {
	FILE *fp=fopen(fn,"r");
	if(fp){
	    if(fscanf(fp,"%hu",&PORT)!=1){
		warning3("Failed to read port from %s\n",fn);
	    }
	    fclose(fp);
	}
    }
    if(PORT==0){
	//user dependent PORT to avoid conflict
	PORT= (uint16_t)((uint16_t)(hashlittle(USER,strlen(USER),0)&0x2FFF)|10000);
    }
    snprintf(fn,PATH_MAX,"%s/.aos/hosts",HOME);
    if(!exist(fn)){
	warning("File %s doesn't exist. "
		"Please create one and put in hostname of "
		"all the computers you are going to run your job in.\n",fn);
	nhost=1;
	hosts=malloc(nhost*sizeof(char*));
	hosts[0]=calloc(60,sizeof(char));
	gethostname(hosts[0],60);
    }else{
	nhost=64;
	hosts=malloc(nhost*sizeof(char*));
	FILE *fp=fopen(fn,"r");
	int ihost=0;
	if(fp){
	    char line[64];
	    while(fscanf(fp,"%s\n",line)==1){
		hosts[ihost]=strdup(line);
		ihost++;
		if(ihost>=nhost){
		    nhost*=2;
		    hosts=realloc(hosts,nhost*sizeof(char*));
		}
	    }
	    fclose(fp);
	    hosts=realloc(hosts,ihost*sizeof(char*));
	    nhost=ihost;
	}else{
	    error("failed to open file %s\n",fn);
	}
    }
 
    char host[60];
    if(gethostname(host,60)) warning3("Unable to get hostname\n");
    hid=myhostid(host);
    if(hid==-1){
	warning3("This host [%s] is not listed in ~/.aos/hosts\n",host);
	//Not running in office machines.
	hosts[0]=strdup(host);//use local machine
	nhost=1;
	hid=myhostid(host);
	if(hid==-1){
	    warning3("Unable to determine proper hostname. Monitor disabled\n");
	}
    }
}

/**
   make a server port and bind to localhost on all addresses
*/
int make_socket (uint16_t port, int retry){
    int sock;
    struct sockaddr_in name;
    
    /* Create the socket. */
    sock = socket (PF_INET, SOCK_STREAM, 0);
    if (sock < 0){
	perror ("socket");
	exit (EXIT_FAILURE);
    }
    
    cloexec(sock);
    setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,NULL,sizeof(int));
    /* Give the socket a name. */
    name.sin_family = AF_INET;
    name.sin_port = htons(port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    int count=0;
    while(bind(sock,(struct sockaddr *)&name, sizeof (name))<0){
	info3("errno=%d. port=%d,sock=%d: ",errno,port,sock);
	perror ("bind");
	if(!retry) return -1;
	sleep(10);
	count++;
	if(count>100){
	    error("Failed to bind to port %d\n",port);
	}
    }
    info3("binded to port %hd at sock %d\n",port,sock);
    return sock;
}
/**
   The following runned_* routines maintains the linked list
   "runned" which contains the already finished jobs.
*/
static int runned_add(RUN_T *irun){
    /*
      Add to the end of the runned list intead of the beggining.
     */
    if(runned_get(irun->pid)){
	warning("runned_add: already exist\n");
	return 1;
    }else{
	//move irun to runned list from running list.
	irun->next=NULL;
	if(runned_end){
	    runned_end->next=irun;
	    runned_end=irun;
	}else{
	    runned_end=runned=irun;
	}
	irun->status.done=1;
	if(irun->status.info<10){
	    warning3("Should be a finished process\n");
	    return 1;
	}else{
	    return 0;
	}
    }
}
static void runned_remove(int pid){
    RUN_T *irun,*irun2=NULL;
    int removed=0;
    for(irun=runned; irun; irun2=irun,irun=irun->next){
	if(irun->pid==pid){
	    irun->status.info=S_REMOVE;
	    monitor_send(irun,NULL);
	    if (irun2){
		irun2->next=irun->next;
		if(irun->next==NULL)
		    runned_end=irun2;
	    }else{
		runned=irun->next;
		if(irun->next==NULL)
		    runned_end=runned;
	    }
	    if(irun->path) free(irun->path);
	    free(irun);
	    removed=1;
	    break;
	}
    }
    if(!removed){
	warning3("runned_remove: Record %s:%d not found!\n",hosts[hid],pid);
    }
}
static RUN_T *runned_get(int pid){
    RUN_T *irun;
    for(irun=runned; irun; irun=irun->next){
	if(irun->pid==pid){
	    break;
	}
    }
    return irun;
}
/**
   The following running_* routiens operates on running linked list
   which contains active jobs.  
*/
static RUN_T* running_add(int pid,int sock){
    RUN_T *irun;
    if((irun=running_get(pid))){
	return irun;
    }else{
	//create the node
	irun=calloc(1, sizeof(RUN_T));
	irun->pid=pid;
	irun->sock=sock;
	//record the launch time
	irun->launchtime=get_job_launchtime(pid);
	irun->next=NULL;
	if(running_end){
	    if(running_end->launchtime <= irun->launchtime){
		//append the node to the end.
		running_end->next=irun;
		running_end=irun;
		irun->next=NULL;
	    }else{
		//insert the node in the middle.
		RUN_T *jrun,*jrun2=NULL;
		for(jrun=running; jrun; jrun2=jrun,jrun=jrun->next){
		    if(jrun->launchtime >= irun->launchtime){
			irun->next=jrun;
			if(jrun2){
			    jrun2->next=irun;
			}else{
			    running=irun;
			}
			break;
		    }
		}
		if(!jrun){
		    warning("failed to insert pid %d\n",pid);
		}
	    }
	}else{
	    running_end=running=irun;
	}
	irun->status.timstart=myclocki();
	FILE *fp=fopen(fnlog,"a");
	fprintf(fp,"[%s] %s %5d  started '%s' nrun=%d\n",
		myasctime(),hosts[hid],pid,irun->path,nrun);
	fclose(fp);
	return irun;
    }
}
static void running_update(int pid, int status){
    RUN_T *irun=running_get(pid);
    //info3("running_update %s:%d with status %d\n",hosts[host],pid,status);
    if(irun){
	if(irun->status.info!=S_WAIT && status>10){
	    nrun-=irun->nthread;//negative decrease
	}
	irun->status.info=status;
	monitor_send(irun,NULL);
	if(status>10){//ended
	    irun->time=(int)myclockd();
	    if(nrun<0) nrun=0;
	    running_remove(pid);//moved to runned.
	    //info3("running_update %s:%d is moved to runned\n",hosts[host],pid);
	}
    }else{
	//warning3("running_update %s:%d not found\n",hosts[host],pid);
    }
}
static void running_remove(int pid){
    RUN_T *irun,*irun2=NULL;
    int removed=0;
    for(irun=running; irun; irun2=irun,irun=irun->next){
	if(irun->pid==pid){
	    if (irun2){
		irun2->next=irun->next;
		if(irun->next==NULL)
		    running_end=irun2;
	    }else{
		running=irun->next;
		if(irun->next==NULL)
		    running_end=running;
	    }
	    irun->status.timend=myclocki();
	    //move irun to runned
	    runned_add(irun);
	    {
		FILE *fp=fopen(fnlog,"a");
		const char *statusstr=NULL;
		switch (irun->status.info){
		case S_CRASH:
		    statusstr="Crashed"; break;
		case S_FINISH:
		    statusstr="Finished";break;
		case S_KILLED:
		    statusstr="Killed";
		default:
		    statusstr="Unknown";break;
		}
		fprintf(fp,"[%s] %s %5d %8s '%s' nrun=%d\n",
			myasctime(),hosts[hid],pid,statusstr,irun->path,nrun);
		fclose(fp);
	    }
	    removed=1;
	    break;
	}
    }
    if(!removed){
	warning3("runned_remove %s:%d not found\n",hosts[hid],pid);
    }
}

static RUN_T *running_get(int pid){
    RUN_T *irun;
    for(irun=running; irun; irun=irun->next){
	if(irun->pid==pid){
	    break;
	}
    }
    return irun;
}

static RUN_T *running_get_wait(void){
    RUN_T *irun;
    int jrun=0;
    for(irun=running; irun; irun=irun->next){
	jrun++;
	if(irun->status.info==S_WAIT){
	    break;
	}
    }
    if(!irun && jrun>0){
	warning("No waiting jobs found. there are %d running jobs\b", jrun);
    }
    return irun;
}

static RUN_T *running_get_by_sock(int sock){
    RUN_T *irun;
    for(irun=running; irun; irun=irun->next){
	if(irun->sock==sock){
	    break;
	}
    }
    return irun;
}
static void check_jobs(void){
    /**
       check all the jobs. remove if any job quited.
     */
    RUN_T *irun;
 restart:
    if(running){
	for(irun=running; irun; irun=irun->next){
	    if(kill(irun->pid,0)){
		running_update(irun->pid,S_CRASH);
		//list is changed. need to restart the loop.
		goto restart;
	    }
	}
    }
}
/**
 examine the process queue to start routines once CPU is available.
*/
static void process_queue(void){
    static double timestamp=0;
    if(nrun>0 && myclockd()-timestamp<10) return;
    timestamp=myclockd();
    if(nrun>=NCPU) return;
    if(nrun<0){
	nrun=0;
    }
    int avail=get_cpu_avail();

    if(avail<1) return;
    RUN_T *irun=running_get_wait();
    if(!irun) {
	all_done=1;
	return;
    }
    
    int nthread=irun->nthread;
    if(nrun+nthread<=NCPU && (nthread<=avail || avail >=3)){
	nrun+=nthread;
	//don't close the socket. will close it in select loop.
	//warning3("process %d launched. write to sock %d cmd %d\n",
	//irun->pid, irun->sock, S_START);
	swriteint(&(irun->sock),S_START);
	irun->status.timstart=myclocki();
	irun->status.info=S_START;
	monitor_send(irun,NULL);
    }
}
/**
   respond to client requests
*/
static int respond(int sock){
    /**
       Don't modify sock in this routine. otherwise select
       will complain Bad file descriptor
    */
    
    int cmd[2];
    int ret=read(sock, cmd, sizeof(int)*2);
   
    if(ret==EOF || ret!=sizeof(int)*2){//remote shutdown write or closed.
	RUN_T *irun=running_get_by_sock(sock);
	if(irun && irun->status.info<10){
	    sleep(1);
	    int pid=irun->pid;
	    if(kill(pid,0)){
		//warning3("Pid %d crashed.\n",pid);
		running_update(pid,S_CRASH);
	    }else{
		//warning3("Pid %d closed socket but is still running\n",pid);
	    }
	}
	//warning("sock %d closed by far end\n",sock);
	/*
	if(!irun){
	    monitor_remove(sock);//may be a monitor.
	    }*/
	return -1;//socket closed.
    }
    int pid=cmd[1];
    switch(cmd[0]){
    case CMD_START:
	{
	    /* Data read. */
	    int nthread=readint(sock);
	    int waiting=readint(sock);
	 
	    if(nthread<1) 
		nthread=1;
	    else if(nthread>NCPU)
		nthread=NCPU;
	    RUN_T *irun=running_add(pid,sock);
	    irun->nthread=nthread;
	    if(waiting){
		irun->status.info=S_WAIT;
		all_done=0;
		info2("%d queued. nrun=%d\n",pid,nrun);
	    }else{//no waiting, no need reply.
		nrun+=nthread;
		irun->status.info=S_START;
		irun->status.timstart=myclocki();
		info2("%d started. nrun=%d\n",pid,nrun);
	    }
	    if(irun->path) monitor_send(irun,irun->path);
	    monitor_send(irun,NULL);
	}
	break;
    case CMD_PATH:{
	RUN_T *irun=running_get(pid);
	if(!irun){
	    irun=running_add(pid,sock);
	}
	if(irun->path) free(irun->path);
	irun->path=readstr(sock);
	info("Received path: %s\n",irun->path);
	monitor_send(irun,irun->path);
    }
	break;
    case CMD_FINISH:
	running_update(pid,S_FINISH);
	return -1;
	break;
    case CMD_CRASH://called by maos
	running_update(pid,S_CRASH);
	return -1;
	break;
    case CMD_STATUS://called by maos
	{
	    RUN_T *irun=running_get(pid);
	    int added=0;
	    if(!irun){
		added=1;
		//started before scheduler is relaunched.
		irun=running_add(pid,sock);
		irun->status.info=S_START;
	    }
	    if(sizeof(STATUS_T)!=read(sock,&(irun->status),
				      sizeof(STATUS_T))){
		warning3("Error reading\n");
	    }
	    if(added){
		irun->nthread=irun->status.nthread;
		nrun+=irun->nthread;
	    }
	    monitor_send(irun,NULL);
	}
	break;
    case CMD_MONITOR://called by monitor
	{
	    MONITOR_T *tmp=monitor_add(sock);
	    if(pid>=0x8){//check monitor version.
		tmp->load=1;
	    }
	}
	break;
    case CMD_KILL://called by mnitor.
	{
	    kill(pid,SIGTERM);
	    running_update(pid,S_KILLED);
	}
	break;
    case CMD_REMOVE://called by monitor to remove a runned object.
	{
	    //int host=readint(sock);//todo: remove this in next run of updates.
	    //(void)host;
	    RUN_T*irun=runned_get(pid);
	    if(irun){
		runned_remove(pid);
	    }else{
		warning3("CMD_REMOVE: %s:%d not found\n",hosts[hid],pid);
	    }
	}
	break;
    case CMD_TRACE:
	{
	    int buflen=200;
	    char out[buflen];
	    char *buf=readstr(sock);
	    FILE *fpcmd=popen(buf,"r");
	    if(!fpcmd){
		writestr(sock,"Command invalid\n");
		break;
	    }
	    free(buf);
	    out[0]='\0';
	    char ans[200];
	    while(fgets(ans, sizeof(ans), fpcmd)){
		char *tmp=strrchr(ans,'/');
		if(tmp){
		    tmp++;
		    char *tmp2=strchr(tmp,'\n'); tmp2[0]='\0';
		    if(strlen(out)+3+strlen(tmp)<sizeof(out)){
			strcat(out, "->");
			strcat(out, tmp);
		    }else
			error("over flow\n");
		    
		}
	    }
	    pclose(fpcmd);
	    writestr(sock,out);
	}
	break;
    case CMD_DRAW:
	{
	    char *display=readstr(sock);
	    setenv("DISPLAY",display,1);
	    char *fifo=readstr(sock);
	    int method=0;
#if defined(__APPLE__)
	    char cmdopen[1024];
	    snprintf(cmdopen, 1024, "open -n -a %s/scripts/drawdaemon.app --args %s", SRCDIR, fifo);
	    if(system(cmdopen)){
		method=0;//failed
	    }else{//succeed
		info("%s succeeded", cmdopen);
		method=3;
	    }
	    if(method==0){
		snprintf(cmdopen, 1024, "open -n -a drawdaemon.app --args %s", fifo);
		if(system(cmdopen)){
		    method=0;//failed
		}else{
		    info("%s succeeded\n", cmdopen);
		    method=3;
		}
	    }
#endif
	    char *fn=stradd(BUILDDIR, "/bin/drawdaemon",NULL);
	    if(method==0){
		info2("Looking for drawdaemon in %s\n",fn);
		if(exist(fn)){
		    info2("Found drawdaemon in %s, run it.\n",fn);
		    method=1;
		}else{
		    warning3("Not found drawdaemon in %s, use bash to find and run drawdaemon.\n",fn);
		    int found=!system("which drawdaemon");
		    if(found){
			method=2;
		    }else{
			warning3("Unable to find drawdaemon\n");
		    }
		}
	    }
	    int ans;
	    if(method==0){
		ans=1;//failed
	    }else{
		ans=0;//succeed
	    }
	    writeint(sock, ans);
	    if(method==3){
		break;//already launched.
	    }
	    //Now start to fork.
	    int pid2=fork();
	    if(pid2<0){
		warning3("Error forking\n");
	    }else if(pid2>0){
		//wait the child so that it won't be a zoombie
		waitpid(pid2,NULL,0);
		break;//continue execution of the parent loop
	    }
	    pid2=fork();
	    if(pid2<0){
		warning3("Error forking\n");
		_exit(EXIT_FAILURE);
	    }else if(pid2>0){
		_exit(EXIT_SUCCESS);//waited by parent.
	    }
	    //safe child.
	    setsid();
	    fclose(stdin);
	    if(method==1){
		if(execl(fn, "drawdaemon",fifo,NULL));
	    }else if(method==2){
		if(execlp("drawdaemon","drawdaemon",fifo,NULL));
	    }else{
		error("Invalid method.\n");
	    }
	}
	break;
    case CMD_SHUTWR:
	shutdown(sock,SHUT_WR);
	break;
    case CMD_SHUTRD:
	shutdown(sock,SHUT_RD);
	break;
    default:
	warning3("Invalid cmd: %d\n",cmd[0]);
    }
    cmd[0]=-1;
    cmd[1]=-1;
    return 0;//don't close the port yet. may be reused by the client.
}

static void scheduler_crash_handler(int sig){
    disable_signal_handler;
    if(sig!=0){
	if(sig==15){
	    warning3("SIGTERM caught. exit\n");
	}else{
	    warning3("signal %d caught. exit\n",sig);
	}
	shutdown(scheduler_sock,SHUT_RDWR);
	for (int i = 0; i < FD_SETSIZE; ++i){
	    if (FD_ISSET (i, &active_fd_set) && i!=scheduler_sock){
		shutdown(i,SHUT_RDWR);
		usleep(100);
		close(i);
		FD_CLR(i, &active_fd_set);
	    }
	}
	close(scheduler_sock);
	usleep(100);
	exit(sig);
    }
}
void scheduler(void){
    register_signal_handler(scheduler_crash_handler);
    fd_set read_fd_set;
    int i;
    struct sockaddr_in clientname;
    socklen_t size;
    /* Create the socket and set it up to accept connections. */
    scheduler_sock = make_socket (PORT, 1);
    if (listen (scheduler_sock, 1) < 0){
	perror ("listen");
	exit (EXIT_FAILURE);
    }

    /* Initialize the set of active sockets. */
    FD_ZERO (&active_fd_set);
    FD_SET (scheduler_sock, &active_fd_set);
    struct timeval timeout;
    timeout.tv_sec=1;
    timeout.tv_usec=0;
    struct timeval *timeout2;
    timeout2=&timeout;
    static int lasttime=0;
    static int lasttime3;
    while (1){
	/* Block until input arrives on one or more active
	   sockets. */
	read_fd_set = active_fd_set;
	if (select (FD_SETSIZE, &read_fd_set, 
		    NULL, NULL, timeout2) < 0){
	    perror("select");
	}
	/* Service all the sockets with input pending. */
	for (i = 0; i < FD_SETSIZE; ++i){
	    if (FD_ISSET (i, &read_fd_set)){
		if (i == scheduler_sock){
		    /* Connection request on original socket. */
		    int new;
		    size = sizeof (clientname);
		    new = accept(scheduler_sock,(struct sockaddr *) 
				 &clientname, &size);
		    if (new < 0){
			perror ("accept");
			exit (EXIT_FAILURE);
		    }
		    cloexec(new);//close on exec.
		    //add fd to watched list.
		    FD_SET (new, &active_fd_set);
		} else {
		    /* Data arriving on an already-connected
		       socket. */
		    if (respond (i) < 0){
			shutdown(i, SHUT_RD);//don't read any more.
			FD_CLR (i, &active_fd_set);//don't monitor any more
			if(monitor_get(i)){
			    //warning("This is a monitor, don't close\n");
			}else{
			    //warning("This is not a monitor. close %d\n",i);
			    close (i);
			}
		    }
		}
	    }
	}
	timeout.tv_sec=1;
	timeout.tv_usec=0;
	timeout2=&timeout;
	int thistime=myclocki();

	if(thistime>=(lasttime+1)){
	    usage_cpu=get_usage_cpu();
	}
	if(!all_done){
	    process_queue();
	}
	if(thistime>=(lasttime3+3)){
	    if(nrun>0){
		check_jobs();
	    }
	    monitor_send_load();
#if USE_POSIX_SHM == 1	    
	    shm_free_unused(NULL,3600);//free shm that are more than 1 hour.
#endif
	    lasttime3=thistime;
	}
	lasttime=thistime;
    }
}

//The following routines maintains the MONITOR_T linked list.
MONITOR_T* monitor_add(int sock){
    //info("added monitor on sock %d\n",sock);
    MONITOR_T *node=calloc(1, sizeof(MONITOR_T));
    node->sock=sock;
    node->next=pmonitor;
    pmonitor=node;
    monitor_send_initial(node);
    return pmonitor;
}
void monitor_remove(int sock){
    MONITOR_T *ic,*ic2=NULL;
    //int freed=0;
    for(ic=pmonitor; ic; ic2=ic,ic=ic->next){
	if(ic->sock==sock){
	    if(ic2){
		ic2->next=ic->next;
	    }else{
		pmonitor=ic->next;
	    }
	    //info("removed monitor on sock %d close it.\n",ic->sock);
	    close(ic->sock);
	    //remove from listening queue.
	    FD_CLR (ic->sock, &active_fd_set);
	    free(ic);
	    //freed=1;
	    break;
	}
    }
    /*
    if(!freed){
	warning3("Monitor record %d not found. (is not a monitor)\n",sock);
	}*/
}
MONITOR_T *monitor_get(int sock){
    MONITOR_T *ic;
    for(ic=pmonitor; ic; ic=ic->next){
	if(ic->sock==sock)
	    break;
    }
    return ic;
}
static int monitor_send_do(RUN_T *irun, char *path, int sock){
    int cmd[3];
    cmd[1]=hid;
    cmd[2]=irun->pid;
    if(path){//don't do both.
	cmd[0]=CMD_PATH;
	if(write(sock,cmd,sizeof(int)*3)!=sizeof(int)*3){
	    perror("write");
	    return 1;
	}
	swritestr(&sock,path);
    }else{
	cmd[0]=CMD_STATUS;
	if(write(sock,cmd,sizeof(int)*3)!=sizeof(int)*3){
	    perror("write");
	    return 1;
	    }
	if(write(sock,&irun->status,sizeof(STATUS_T))!=sizeof(STATUS_T)){
	    perror("write");
	    return 1;
	}
    }
    return 0;
}
// Notify alreadyed connected monitors job update.
void monitor_send(RUN_T *irun,char*path){
    MONITOR_T *ic;
 redo:
    for(ic=pmonitor; ic; ic=ic->next){
	//sock=monitor_connect(hosts[ic->id]);
	int sock=ic->sock;
	if(monitor_send_do(irun,path,sock)){
	    //warning3("Send to monitor %d failed. "
	    //		     "Remove monitor and restart.\n",sock);
	    monitor_remove(sock);
	    goto redo;
	}
    }
}
// Notify alreadyed connected monitors machine load.
void monitor_send_load(void){
    MONITOR_T *ic;
    double mem=get_usage_mem();
    //info("usage_cpu=%g, mem=%g\n",usage_cpu,mem);
    int cmd[3];
    cmd[0]=CMD_LOAD;
    cmd[1]=hid;
    int memi=(int)(mem*100);
    int cpui=(int)(usage_cpu*100);
    cmd[2]=memi | (cpui << 16);
 redo:
    for(ic=pmonitor; ic; ic=ic->next){
	int sock=ic->sock;
	if(!ic->load)
	    continue;

	if(write(sock,cmd,sizeof(int)*3)!=sizeof(int)*3){
	    //perror("write");
	    //warning3("Unabled to send cmd\n");
	    monitor_remove(sock);
	    goto redo;
	}
    }
}
// Notify the new added monitor all job information.
void monitor_send_initial(MONITOR_T *ic){
    //info("Monitor_send_initial\n");
    int sock;
    RUN_T *irun;
    sock=ic->sock;
    {
	//send version information.
	int cmd[3];
	cmd[0]=CMD_VERSION;
	cmd[1]=scheduler_version;
	cmd[2]=hid;//Fixme: sending hid to monitor is not good is hosts does not match.
	if(write(sock,cmd,sizeof(int)*3)!=sizeof(int)*3){
	    //perror("write");
	    //warning3("Unabled to send version to monitor.\n");
	    return;
	}
    }
    for(irun=runned; irun; irun=irun->next){
	if(monitor_send_do(irun,irun->path,sock)||
	   monitor_send_do(irun,NULL,sock)){
	    monitor_remove(sock);
	    return;
	}   
    }
    for(irun=running; irun; irun=irun->next){
	if(monitor_send_do(irun,irun->path,sock)||
	   monitor_send_do(irun,NULL,sock)){
	    monitor_remove(sock);
	    return;
	}
    }
    //info("Monitor_send_initial success\n");
}
#endif