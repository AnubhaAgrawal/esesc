/*
ESESC: Enhanced Super ESCalar simulator
Copyright (C) 2009 University of California, Santa Cruz.

Contributed by Jose Renau, Sina Hassani

This file is part of ESESC.

ESESC is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation;
either version 2, or (at your option) any later version.

ESESC is    distributed in the  hope that  it will  be  useful, but  WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should  have received a copy of  the GNU General  Public License along with
ESESC; see the file COPYING.  If not, write to the  Free Software Foundation, 59
Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include "../misc/libsuc/transporter.h" 

//Declaring global variables
typedef uint32_t FlowID; // from RAWDInst.h
void *handle;
int64_t inst_count = 0;
int thread_mode = 0;
int child_id, portno;
char host_adr[100];
int64_t nrabbit = 0;
int ncheckpoints = 0;
int nchecks = 0;
int64_t live_skip = 0;
bool done_skip = false;
int force_warmup = 0;
int genwarm = 0;

//Declaring functions
extern "C" void QEMUReader_goto_sleep(void *env);
extern "C" void QEMUReader_wakeup_from_sleep(void *env);
extern "C" int64_t qemuesesc_main(int64_t argc, char **argv, char **envp);
extern "C" void esesc_set_timing(uint32_t fid);

//Defining dynamic functions
typedef uint64_t (*dyn_QEMUReader_getFid_t)(uint32_t);
dyn_QEMUReader_getFid_t dyn_QEMUReader_getFid=0;

typedef void (*dyn_QEMUReader_finish_thread_t)(uint32_t);
dyn_QEMUReader_finish_thread_t dyn_QEMUReader_finish_thread=0;

typedef uint64_t (*dyn_QEMUReader_get_time_t)();
dyn_QEMUReader_get_time_t dyn_QEMUReader_get_time=0;

typedef uint32_t (*dyn_QEMUReader_setnoStats_t)();
dyn_QEMUReader_setnoStats_t dyn_QEMUReader_setnoStats=0;

typedef FlowID (*dyn_QEMUReader_resumeThreadGPU_t)(FlowID);
dyn_QEMUReader_resumeThreadGPU_t dyn_QEMUReader_resumeThreadGPU=0;

typedef FlowID (*dyn_QEMUReader_resumeThread_t)(FlowID, FlowID);
dyn_QEMUReader_resumeThread_t dyn_QEMUReader_resumeThread=0;

typedef void (*dyn_QEMUReader_pauseThread_t)(FlowID);
dyn_QEMUReader_pauseThread_t dyn_QEMUReader_pauseThread=0;

typedef void (*dyn_QEMUReader_queue_inst_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint64_t, void *);
dyn_QEMUReader_queue_inst_t dyn_QEMUReader_queue_inst=0;

typedef void (*dyn_QEMUReader_syscall_t)(uint32_t, uint64_t, uint32_t);
dyn_QEMUReader_syscall_t dyn_QEMUReader_syscall=0;

typedef void (*dyn_QEMUReader_finish_t)(uint32_t);
dyn_QEMUReader_finish_t dyn_QEMUReader_finish=0;

typedef void (*dyn_QEMUReader_setFlowCmd_t)(bool*);
dyn_QEMUReader_setFlowCmd_t dyn_QEMUReader_setFlowCmd=0;

//This function loads the master rabbit dynamic library
void load_rabbit () {
  printf("Live: Loading rabbit\n");
  handle = dlopen("librabbitso.so", RTLD_NOW);
  if (!handle) {
    printf("DLOPEN: %s\n", dlerror());
    exit(EXIT_FAILURE);
  }

  //Initializing dynamic functions
  dyn_QEMUReader_getFid          = (dyn_QEMUReader_getFid_t)dlsym(handle, "QEMUReader_getFid");
  dyn_QEMUReader_finish_thread   = (dyn_QEMUReader_finish_thread_t)dlsym(handle, "QEMUReader_finish_thread");
  dyn_QEMUReader_get_time        = (dyn_QEMUReader_get_time_t)dlsym(handle, "QEMUReader_get_time");
  dyn_QEMUReader_setnoStats      = (dyn_QEMUReader_setnoStats_t)dlsym(handle, "QEMUReader_setnoStats");
  dyn_QEMUReader_resumeThreadGPU = (dyn_QEMUReader_resumeThreadGPU_t)dlsym(handle, "QEMUReader_resumeThreadGPU");
  dyn_QEMUReader_resumeThread    = (dyn_QEMUReader_resumeThread_t)dlsym(handle, "QEMUReader_resumeThread");
  dyn_QEMUReader_pauseThread     = (dyn_QEMUReader_pauseThread_t)dlsym(handle, "QEMUReader_pauseThread");
  dyn_QEMUReader_queue_inst      = (dyn_QEMUReader_queue_inst_t)dlsym(handle, "QEMUReader_queue_inst");
  dyn_QEMUReader_syscall         = (dyn_QEMUReader_syscall_t)dlsym(handle, "QEMUReader_syscall");
  dyn_QEMUReader_finish          = (dyn_QEMUReader_finish_t)dlsym(handle, "QEMUReader_finish");
  dyn_QEMUReader_setFlowCmd      = (dyn_QEMUReader_setFlowCmd_t)dlsym(handle, "QEMUReader_setFlowCmd");

  printf("Live: Rabbit setup done\n");
}

void unload_rabbit () {
  dlclose(handle);
}

//This function is used for loading ESESC dynamic library
void load_esesc () {
  static bool done = false;
  if (done) {
    //printf("+");
    return;
  }
  done = true;

  //printf("Live: Checkpoint64_t %ld is loading ESESC\n", child_id);

  handle = dlopen("libesescso.so", RTLD_NOW);
  if (!handle) {
    printf("DLOPEN: %s\n", dlerror());
    exit(EXIT_FAILURE);
  }

  //Initializing dynamci functions
  dyn_QEMUReader_getFid          = (dyn_QEMUReader_getFid_t)dlsym(handle, "QEMUReader_getFid");
  dyn_QEMUReader_finish_thread   = (dyn_QEMUReader_finish_thread_t)dlsym(handle, "QEMUReader_finish_thread");
  dyn_QEMUReader_get_time        = (dyn_QEMUReader_get_time_t)dlsym(handle, "QEMUReader_get_time");
  dyn_QEMUReader_setnoStats      = (dyn_QEMUReader_setnoStats_t)dlsym(handle, "QEMUReader_setnoStats");
  dyn_QEMUReader_resumeThreadGPU = (dyn_QEMUReader_resumeThreadGPU_t)dlsym(handle, "QEMUReader_resumeThreadGPU");
  dyn_QEMUReader_resumeThread    = (dyn_QEMUReader_resumeThread_t)dlsym(handle, "QEMUReader_resumeThread");
  dyn_QEMUReader_pauseThread     = (dyn_QEMUReader_pauseThread_t)dlsym(handle, "QEMUReader_pauseThread");
  dyn_QEMUReader_queue_inst      = (dyn_QEMUReader_queue_inst_t)dlsym(handle, "QEMUReader_queue_inst");
  dyn_QEMUReader_syscall         = (dyn_QEMUReader_syscall_t)dlsym(handle, "QEMUReader_syscall");
  dyn_QEMUReader_finish          = (dyn_QEMUReader_finish_t)dlsym(handle, "QEMUReader_finish");
  dyn_QEMUReader_setFlowCmd      = (dyn_QEMUReader_setFlowCmd_t)dlsym(handle, "QEMUReader_setFlowCmd");

  typedef void (*dyn_start_esesc_t)(char *, int, int, int, int);
  dyn_start_esesc_t dyn_start_esesc = (dyn_start_esesc_t)dlsym(handle, "start_esesc");
  if (dyn_start_esesc==0) {
    printf("DLOPEN no start_esesc: %s\n", dlerror());
    exit(-3);
  }

  (*dyn_start_esesc)(host_adr, portno, child_id, force_warmup, genwarm); // FIXME: start_esesc should be a separate thread (qemu thread should wait until this is finished)
}

//This function is used to initialize the emulator in order to fork checkpoints
void create_checkpoints (int64_t argc, char **argv) {
  char **qargv = (char **) malloc(argc*sizeof(char **));
  qargv = (char **)malloc(argc*sizeof(char*));
  qargv[0] = (char *) "live";
  for(int64_t j = 1; j < argc; j++) {
    qargv[j] = strdup(argv[j]);
  }

  nchecks = ncheckpoints + 1;
  load_rabbit();
  child_id = 0;
  qemuesesc_main(argc,qargv,0);
}

//This function creates a checkpoint64_t making it ready for simulation
void fork_checkpoint() {
  if (fork() != 0) {
    //Parent: initialize new variables and continue rabbit
    thread_mode = 0;
    inst_count = 0;
    child_id++;
    return;
  }
    
  //Children: create the checkpoint
  //Send the ready command to NodeJS server
  Transporter::disconnect();
  Transporter::connect_to_server(host_adr, portno);
  Transporter::send_fast("reg_cp", "%d,%d", child_id, getpid());
  thread_mode = 1;
  inst_count = 0;
  bool wait = true;
  int k;

  //Go to waiting mode
  while(wait) {
    Transporter::receive_fast("simulate", "%d,%d,%d", &k, &force_warmup, &genwarm);
    if(k == 1)
      exit(0);
    //On simulation request, create a new process to continue simulation and return to waiting mode
    signal(SIGCHLD, SIG_IGN);
    int pid = fork();
    if(pid == 0) {
      Transporter::disconnect();
      wait = false;
      thread_mode = 2;
      unload_rabbit();
      load_esesc();
    }
  }

  return;
}

//Qemu/ESESC interface functions
extern "C" uint32_t QEMUReader_getFid (uint32_t last_fid) {
  return (*dyn_QEMUReader_getFid)(last_fid);
}

extern "C" void QEMUReader_finish_thread (uint32_t fid) {
  return (*dyn_QEMUReader_finish_thread)(fid);
}

extern "C" uint64_t QEMUReader_get_time () {
  return (*dyn_QEMUReader_get_time)();
}

extern "C" uint32_t QEMUReader_setnoStats () {
  return (*dyn_QEMUReader_setnoStats)();
}

extern "C" FlowID QEMUReader_resumeThreadGPU (FlowID uid) {
  return (*dyn_QEMUReader_resumeThreadGPU)(uid);
}

extern "C" FlowID QEMUReader_resumeThread (FlowID uid, FlowID last_fid) {
  return (*dyn_QEMUReader_resumeThread)(uid, last_fid);
}

extern "C" void QEMUReader_pauseThread (FlowID id) {
  (*dyn_QEMUReader_pauseThread)(id);
}

extern "C" void QEMUReader_queue_inst (uint32_t insn, uint32_t pc, uint32_t addr, uint32_t fid, uint32_t op, uint64_t icount, void *env) {
  (*dyn_QEMUReader_queue_inst)(insn,pc,addr,fid,op,icount,env);
  if(!done_skip && inst_count < live_skip) {
    inst_count += icount;
    return;
  }
  if(!done_skip) {
    done_skip = true;
    inst_count = nrabbit;
    return;
  }
  if(thread_mode == 0) {
    inst_count += icount;
    if(inst_count > nrabbit) {
      nchecks--;
      if (nchecks<=0) {
        //printf("Master Rabbit done\n");
        exit(0);
      }
      fork_checkpoint();
    }
  }
}

extern "C" void QEMUReader_syscall (uint32_t num, uint64_t usecs, uint32_t fid) {
  (*dyn_QEMUReader_syscall)(num,usecs,fid);
}

extern "C" void QEMUReader_finish (uint32_t fid) {
  (*dyn_QEMUReader_finish)(fid);
}

extern "C" void QEMUReader_setFlowCmd (bool* flowStatus) {
  (*dyn_QEMUReader_setFlowCmd)(flowStatus);
}

//Main function
int main (int argc, char **argv) {
  printf("foo");
  char t;
  int64_t params[11];
  int64_t i;
  //Setting Qemu parameters
  char **qparams;
  int64_t qparams_argc= argc - 3;
  qparams = (char **)malloc(sizeof(char *)*qparams_argc+1);
  qparams[0] = strdup("../main/live");
  for(i = 1; i < qparams_argc; i++) {
    qparams[i] = strdup(argv[i + 3]);
  }
  qparams[qparams_argc] = 0;

  //Connecting to the TCP server (Running on Node.js)
  strcpy(host_adr, argv[2]);
  portno = atoi(argv[3]);
  Transporter::connect_to_server(host_adr, portno);
  Transporter::send_fast("reg_daemon", "%d", getpid());
  Transporter::receive_fast("reg_ack", "");
  printf("Registered on server\n");

  //Waiting for command
  do {
    printf("Waiting for request ... \n");
    Transporter::receive_fast("config", "%d,%ld,%ld", &ncheckpoints, &nrabbit, &live_skip);
    if(fork() == 0) {
      Transporter::disconnect();
      Transporter::connect_to_server(host_adr, portno);
      Transporter::send_fast("reg_conf", "%d", getpid());
      Transporter::receive_fast("reg_ack", "");
      create_checkpoints(qparams_argc, qparams);
    }
  } while(1);

  Transporter::disconnect();
  return 0;
}
