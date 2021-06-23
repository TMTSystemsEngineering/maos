/*
  Copyright 2009-2021 Lianqi Wang <lianqiw-at-tmt-dot-org>
  
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

/*
  All routines in this thread runs in a separate threads from the main
  thread. In order to call any gdk/gtk routines, it calls to go through
  gdk_threads_add_idle. The thread is waits for select() results.
*/


#include <sys/socket.h>
#include "../sys/sys.h"
#include "monitor.h"
proc_t** pproc;
int* nproc;
extern int* hsock;   //socket of each host
static double* htime;//last time having signal from each host.
int nhostup=0;
PNEW(mhost);
int sock_main[2]={0,0}; /*Use to talk to the thread that blocks in select()*/
static fd_set active_fd_set;
//int pipe_addhost[2]={0,0};
extern double* usage_cpu, * usage_cpu2;
//extern double *usage_mem, *usage_mem2;
/*
static proc_t *proc_add(int id,int pid){
	proc_t *iproc;
	if((iproc=proc_get(id,pid))) return iproc;
	iproc=mycalloc(1,proc_t);
	iproc->iseed_old=-1;
	iproc->pid=pid;
	iproc->hid=id;
	LOCK(mhost);
	iproc->next=pproc[id];
	pproc[id]=iproc;
	nproc[id]++;
	gdk_threads_add_idle((GSourceFunc)update_title, GINT_TO_POINTER(id));
	UNLOCK(mhost);
	return iproc;
}
*/
proc_t* proc_get(int id, int pid){
	proc_t* iproc;
	if(id<0||id>=nhost){
		error("id=%d is invalid\n", id);
	}
	LOCK(mhost);
	for(iproc=pproc[id]; iproc; iproc=iproc->next){
		if(iproc->pid==pid){
			break;
		}
	}
	if(!iproc){
		//info("%s: %d is not found\n", hosts[id], pid);
		iproc=mycalloc(1, proc_t);
		//iproc->iseed_old=-1;
		iproc->pid=pid;
		iproc->hid=id;

		iproc->next=pproc[id];
		pproc[id]=iproc;
		nproc[id]++;
		gdk_threads_add_idle((GSourceFunc)update_title, GINT_TO_POINTER(id));
	}
	UNLOCK(mhost);
	return iproc;
}


static void proc_remove_all(int id){
	proc_t* iproc, * jproc=NULL;
	LOCK(mhost);
	for(iproc=pproc[id]; iproc; iproc=jproc){
		jproc=iproc->next;
		gdk_threads_add_idle((GSourceFunc)remove_entry, iproc);//frees iproc
	}
	nproc[id]=0;
	UNLOCK(mhost);
	pproc[id]=NULL;
	gdk_threads_add_idle((GSourceFunc)update_title, GINT_TO_POINTER(id));
}

static void proc_remove(int id, int pid){
	proc_t* iproc, * jproc=NULL;
	for(iproc=pproc[id]; iproc; jproc=iproc, iproc=iproc->next){
		if(iproc->pid==pid){
			LOCK(mhost);
			if(jproc){
				jproc->next=iproc->next;
			} else{
				pproc[id]=iproc->next;
			}
			nproc[id]--;
			UNLOCK(mhost);
			gdk_threads_add_idle((GSourceFunc)remove_entry, iproc);
			gdk_threads_add_idle((GSourceFunc)update_title, GINT_TO_POINTER(id));
			break;
		}
	}
}


static int host_from_sock(int sock){
	if(sock<0) return -1;
	for(int ihost=0; ihost<nhost; ihost++){
		if(hsock[ihost]==sock){
			return ihost;
		}
	}
	return -1;
}


void add_host_wrap(int ihost){
	int cmd[3]={MON_ADDHOST, ihost, 0};
	stwriteintarr(sock_main[1], cmd, 3);
}

/* Record the host after connection is established*/
static void host_added(int ihost, int sock){
	htime[ihost]=myclockd();
	proc_remove_all(ihost);/*remove all entries. */
	LOCK(mhost);
	nhostup++;
	hsock[ihost]=sock;
	FD_SET(sock, &active_fd_set);
	UNLOCK(mhost);
	add_host_wrap(-1);//wake to use new active_fd_set
	gdk_threads_add_idle(host_up, GINT_TO_POINTER(ihost));
}

/*remove the host upon disconnection*/
static void host_removed(int sock){
	int ihost=host_from_sock(sock);
	if(ihost==-1) return;
	close(sock);
	LOCK(mhost);
	nhostup--;
	hsock[ihost]=-1;
	FD_CLR(sock, &active_fd_set);
	UNLOCK(mhost);
	add_host_wrap(-1);//wake to use new active_fd_set
	gdk_threads_add_idle(host_down, GINT_TO_POINTER(ihost));
	info("disconnected from %s\n", hosts[ihost]);
}
//connect to scheduler(host)
static void* add_host(gpointer data){
	int ihost=GPOINTER_TO_INT(data);
	int todo=0;
	LOCK(mhost);
	if(hsock[ihost]==-1){
		hsock[ihost]--;//make it -2 so no concurrent access.
		todo=1;
	}
	UNLOCK(mhost);
	if(todo){
		int sock=connect_port(hosts[ihost], PORT, 0, 0);
		if(sock>-1){
			int cmd[2];
			cmd[0]=CMD_MONITOR;
			cmd[1]=scheduler_version;
			if(stwriteintarr(sock, cmd, 2)){//write failed.
				warning_time("Failed to write to scheduler at %s\n", hosts[ihost]);
				close(sock);
				sock=-1;
			} else{
				host_added(ihost, sock);
			}
		}
		if(sock<0){
			dbg_time("Cannot reach %s", hosts[ihost]);
			LOCK(mhost);
			hsock[ihost]=-1;
			UNLOCK(mhost);
		}
	}
	return NULL;
}
//Calls add_host in a new thread. Used by the thread running listen_host().
static void add_host_thread(int ihost){
	if(ihost>-1&&ihost<nhost){
		if(hsock[ihost]==-1){
			pthread_t tmp;
			pthread_create(&tmp, NULL, add_host, GINT_TO_POINTER(ihost));
			//add_host(GINT_TO_POINTER(ihost));
		} else{
			dbg_time("MON_ADDHOST: hsock[%d]=%d\n", ihost, hsock[ihost]);
		}
	}
}
//called by listen_host to respond to scheduler
static int respond(int sock){
	int cmd[3];
	//read fixed length header info.
	if(streadintarr(sock, cmd, 3)){
		return -1;//failed
	}
	int ihost=host_from_sock(sock);
	if(ihost>=0){
		htime[ihost]=myclockd();
	}
	int pid=cmd[2];
	switch(cmd[0]){
	case -1:{//server request shutdown
		return -1;
	}
	break;
	case MON_DRAWDAEMON:
	{
		dbg_time("Received drawdaemon request\n");
		scheduler_display(ihost, 0);
	}
	break;
	case MON_STATUS:
	{
		if(ihost<0){
			warning("Host not found\n");
			return -1;
		}
		proc_t* p=proc_get(ihost, pid);
		/*if(!p){
		p=proc_add(ihost,pid);
		}*/
		if(stread(sock, &p->status, sizeof(status_t))){
			return -1;
		}
		if(p->status.info==S_REMOVE){
			proc_remove(ihost, pid);
		} else{
			if(cmd[1]!=ihost&&cmd[1]!=cmd[2]){
				/*A new mean to replace the ID of a job.*/
				p->pid=cmd[1];
			}
			gdk_threads_add_idle((GSourceFunc)refresh, p);
		}
	}
	break;
	case MON_PATH:
	{
		if(ihost<0){
			warning("Host not found\n");
			return -1;
		}
		proc_t* p=proc_get(ihost, pid);
		/*if(!p){
		p=proc_add(ihost,pid);
		}*/
		if(streadstr(sock, &p->path)){
			return -1;
		}
		char* tmp=NULL;
		while((tmp=strchr(p->path, '\n'))){
			tmp[0]=' ';
		}
	}
	break;
	case MON_VERSION:
		break;
	case MON_LOAD:
	{
		if(ihost<0){
			warning("Host not found\n");
			return -1;
		}
		usage_cpu[ihost]=(double)((pid>>16)&0xFFFF)/100.;
		//usage_mem[ihost]=(double)(pid & 0xFFFF)/100.;
		usage_cpu[ihost]=MAX(MIN(1, usage_cpu[ihost]), 0);
		//usage_mem[ihost]=MAX(MIN(1,usage_mem[ihost]),0);
		gdk_threads_add_idle((GSourceFunc)update_progress, GINT_TO_POINTER(ihost));
	}
	break;
	case MON_ADDHOST:
		if(cmd[1]>-1&&cmd[1]<nhost){
			add_host_thread(cmd[1]);
		} else if(cmd[1]==-2){//quit
			return -2;
		}
		break;
	default:
		warning_time("Invalid cmd %d\n", cmd[0]);
		return -1;
	}
	return 0;
}
/**
   listen_host() live in a separate thread, it has the following resposibilities:
   1) listening commands from the main thread to initiate connection to servers
   2) listening to connected servers for maos status event and update the display
   3) monitor connected servers for activity. Disable pages when server is disconnected.

   write to sock_main[1] will be caught by select in listen_host(). This wakes it up.*/
void* listen_host(void* dummy){
	(void)dummy;
	htime=mycalloc(nhost, double);
	FD_ZERO(&active_fd_set);
	FD_SET(sock_main[0], &active_fd_set);//listen to monitor itself
	int keep_listen=1;
	while(keep_listen){
		fd_set read_fd_set=active_fd_set;
		struct timeval timeout={5,0};
		if(select(FD_SETSIZE, &read_fd_set, NULL, NULL, &timeout)<0){
			perror("select");
			continue;
		}
		for(int i=0; i<FD_SETSIZE; i++){
			if(FD_ISSET(i, &read_fd_set)){
				int res;
				res=respond(i);
				if(res==-2){//quit
					keep_listen=0;
					break;
				} else if(res==-1){//remove host
					host_removed(i);
				}
			}
		}
		double ntime=myclockd();
		for(int ihost=0; ihost<nhost; ihost++){
			if(htime[ihost]){//only handle hosts that are ever connected
				if(hsock[ihost]<0){//disconnected, trying to reconnect
					if(ntime>htime[ihost]+60){//try every 600 seconds
						htime[ihost]=ntime;
						add_host_thread(ihost);//do not use _add_host_wrap. It will deadlock.
					}
				}else if(htime[ihost]>0 && ntime>htime[ihost]+60){//no activity for 60 seconds. check host connection 
					dbg_time("60 seconds no respond. probing server %s.\n", hosts[ihost]);
					scheduler_cmd(ihost, 0, CMD_PROBE);
					htime[ihost]=-ntime;
				}else if(htime[ihost]<0 && ntime>-htime[ihost]+60){//probed, but not response within 60 seconds
					dbg_time("no respond. disconnect server %s.\n", hosts[ihost]);
					host_removed(hsock[ihost]);
				}
			}
		}
	}
	for(int i=0; i<FD_SETSIZE; i++){
		if(FD_ISSET(i, &active_fd_set)){
			close(i);
			FD_CLR(i, &active_fd_set);
		}
	}
	return NULL;
}
/**
   called by monitor to let a MAOS job remotely draw on this computer
*/
int scheduler_display(int ihost, int pid){
	if(ihost<0) return 0;
	dbg_time("schedueler_display: %s, pid=%d", hosts[ihost], pid);
	/*connect to scheduler with a new port. The schedule pass the other
	  end of the port to drawdaemon so we can communicate with it.*/
	int sock=connect_port(hosts[ihost], PORT, 0, 0);
	int ans=1;
	int cmd[2]={CMD_DISPLAY, pid};
	if(stwriteintarr(sock, cmd, 2)||streadintarr(sock, cmd, 1)||cmd[0]){
		warning("Failed to pass sock to draw via scheduler.\n");
		close(sock);
	} else{
		char arg1[20];
		snprintf(arg1, 20, "%d", sock);
		if(spawn_process("drawdaemon", arg1, NULL)<0){
			warning("spawn drawdaemon failed\n");
		} else{
			ans=0;
		}
		close(sock);
	}
	return ans;
}
/**
   called by monitor to talk to scheduler.
*/
int scheduler_cmd(int ihost, int pid, int command){
	if(ihost<0) return 0;
	if(command==CMD_DISPLAY){
		return scheduler_display(ihost, pid);
	} else{
		int sock=hsock[ihost];
		if(sock<0){
			add_host_thread(ihost);
			sleep(1);
			sock=hsock[ihost];
		}
		if(sock<0) return 1;
		int cmd[2];
		cmd[0]=command;
		cmd[1]=pid;/*pid */
		int ans=stwriteintarr(sock, cmd, 2);
		if(ans){/*communicated failed.*/
			host_removed(ihost);
		}
		return ans;
	}
}
