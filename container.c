#include <sys/cdefs.h>

#include "opt_compat.h"
#include "opt_kdtrace.h"
#include "opt_ktrace.h"
#include "opt_core.h"
#include "opt_procdesc.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/acct.h>
#include <sys/capability.h>
#include <sys/condvar.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/imgact.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/ktrace.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/procdesc.h>
#include <sys/posix4.h>
#include <sys/pioctl.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/sdt.h>
#include <sys/sbuf.h>
#include <sys/sleepqueue.h>
#include <sys/smp.h>
#include <sys/stat.h>
#include <sys/sx.h>
#include <sys/syscallsubr.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/syslog.h>
#include <sys/sysproto.h>
#include <sys/timers.h>
#include <sys/unistd.h>
#include <sys/wait.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/uma.h>
#include <sys/jail.h>
#include <machine/cpu.h>
#include <security/audit/audit.h>

#include <sys/libkern.h>//for strcmp(),strcpy(),strlen().
#include <sys/Project0.h>

#ifndef _SYS_SYSPROTO_H_
struct set_containerid_args {

       int idnum; 
       pid_t pid;
};

struct create_container_args { 

       int perms;
       char *name;
};

struct destroy_container_args {

       char *name;
};

struct write_container_args {

       char *name;
       char *message;
       int len;
};

struct read_container_args {

       char *name;
       char *message;
       int len;

};

#endif

struct node {
  
  char *name;//container name
  char *data;//the message in the container
  struct node *next;//next container
  uid_t uid;//created by which user
  int ctid;//container id number of the calling process
  int perms;//whether allowed to run by other users
  int msgstate;//state: initial(0),red(1),waiting for read(2),written(3),waiting for write(4).
  int currenMsgred;//red(1),not red(0).
  void *read_wchan;
  void *write_wchan;
};




static struct node *Container_head = NULL;
struct mtx mutex;

MTX_SYSINIT(mutex,&mutex,"container mutex", MTX_DEF);
MALLOC_DEFINE(M_NODE, "A Newnode", "A Newnode created in the container linked list");

//void *read_wchan;
//void *write_wchan;

int	
sys_set_containerid(struct thread *td, struct set_containerid_args *uap)
{
    struct proc *p;
   
    AUDIT_ARG_PID(uap->pid);
    //only root user can set container id
	if(td->td_ucred->cr_ruid == 0)
	{
	    mtx_lock(&mutex);
	
		if(uap->pid == 0)
	   {
         td->td_proc->p_containerid = uap->idnum;	       
	   }
	    else if(uap->pid > 0)
	   {
	     p = pfind(uap->pid);//PROC_LOCK(p)
		 
	     if (p == NULL)
		 {
		    mtx_unlock(&mutex);
		    return ESRCH;
		 }
		 
		 AUDIT_ARG_PROCESS(p);
		 
		 p->p_containerid = uap->idnum;
		 
		 PROC_UNLOCK(p);
	   }
	  
	   mtx_unlock(&mutex);
	   					  
	   return 0;
	}
	else
	{
	  return EPERM;//return error!
	}
	
}


int	
sys_create_container(struct thread *td, struct create_container_args *uap)
{
  
   struct node *cursor;//for traverse.
   struct node *prev;
   struct node *Newnode;
   char *kern_name;
   
   kern_name = (char *)malloc(300, M_NODE, M_NOWAIT);
   bzero(kern_name,300);
   
   copyinstr(uap->name,kern_name,300,NULL);
   
   mtx_lock(&mutex);  
     
   if(strlen(kern_name) > 255)
   {
     mtx_unlock(&mutex);
     return ENAMETOOLONG;//Error: container name too long.
   }
     
   //add the container
   if(Container_head == NULL)
   {
	 Container_head = malloc(sizeof(struct node), M_NODE, M_NOWAIT);
	 Container_head->name = malloc(256, M_NODE, M_NOWAIT);
	 
	 copystr(kern_name,Container_head->name,256,NULL);
	 //strcpy(Container_head->name,kern_name);
	 Container_head->data = NULL;
         Container_head->next = NULL;
	 Container_head->uid = td->td_ucred->cr_ruid;
         Container_head->ctid = td->td_proc->p_containerid; 
	 Container_head->perms = uap->perms;
	 Container_head->msgstate = 0;
	 Container_head->currenMsgred = 0;
	 Container_head->read_wchan = malloc(255, M_NODE, M_NOWAIT);
         Container_head->write_wchan = malloc(255, M_NODE, M_NOWAIT);
   }
   else //add to the end
   {
          cursor = Container_head;
	  prev = Container_head;
	  
	  while(cursor != NULL)
       {
	   if( strcmp(cursor->name,kern_name) == 0 && (cursor->ctid == td->td_proc->p_containerid) )
           {
	     mtx_unlock(&mutex);
	     return EEXIST;//Error: Name already existed.
	   }	   
	   
	   prev = cursor;
	   cursor = cursor->next;
       }
	  
	  Newnode = malloc(sizeof(struct node), M_NODE, M_NOWAIT);
	  prev->next = Newnode;  
	  Newnode->name = malloc(256, M_NODE, M_NOWAIT);
	  
	  copystr(kern_name,Newnode->name,256,NULL);
	  //strcpy(Newnode->name,kern_name);
	  Newnode->data = NULL;
	  Newnode->next = NULL;
          Newnode->uid = td->td_ucred->cr_ruid;
          Newnode->ctid = td->td_proc->p_containerid; 
	  Newnode->perms = uap->perms;
	  Newnode->msgstate = 0;
	  Newnode->currenMsgred = 0;
	  Newnode->read_wchan = malloc(255, M_NODE, M_NOWAIT);
          Newnode->write_wchan = malloc(255, M_NODE, M_NOWAIT);
   }
  
    mtx_unlock(&mutex);
	
    return 0;
}

int	
sys_destroy_container(struct thread *td, struct destroy_container_args *uap)
{
  struct node *cursor;
  struct node *prev ;
  struct node *destroynode = NULL;  
  char *kern_name;
  
  kern_name = (char *)malloc(256, M_NODE, M_NOWAIT);
  bzero(kern_name,256);
  
  copyinstr(uap->name,kern_name,256,NULL);
  
  mtx_lock(&mutex);
  
  //find the one to be destroyed container
  cursor = Container_head;
  prev = Container_head;
  
  while(cursor != NULL)
  {
    if(strcmp(cursor->name,kern_name) == 0 && cursor->ctid == td->td_proc->p_containerid)
	{
	   destroynode = cursor;
	   break;
	}
	
	prev = cursor;
	cursor = cursor->next;
  }
  
  if(destroynode == NULL)
  {
     mtx_unlock(&mutex);
     return ENOENT;//Error: Destroyed container not exist.
  }
  
  if(td->td_ucred->cr_ruid != 0 && destroynode->uid != td->td_ucred->cr_ruid)
  {
     mtx_unlock(&mutex);
     return EPERM;//Error: Operation destroy not permitted.
  }
  
   //If delete linked list head:
   if(strcmp(destroynode->name,Container_head->name) == 0)
   {
     Container_head = destroynode->next;
	 //wakeup(destroynode->write_wchan);
	 //free(destroynode,M_NODE);
   }
   else
   {
     prev->next = destroynode->next;
	 //wakeup(destroynode->write_wchan);
     //free(destroynode,M_NODE);
   }
   
   wakeup(destroynode->write_wchan);
   free(destroynode,M_NODE);
   
   destroynode = NULL;
  
   mtx_unlock(&mutex);
   
   return 0;
}

int	
sys_write_container(struct thread *td, struct write_container_args *uap)
{
  struct node *cursor;//traverse the linked list
  struct node *writenode = NULL;
  struct node *checknode = NULL;  
  char *kern_name;
  char *kern_msg;
  //char *addr;
 
  kern_name = malloc(256, M_NODE, M_NOWAIT);
  kern_msg = malloc(300, M_NODE, M_NOWAIT);
  
  bzero(kern_name,256);
  bzero(kern_msg,300);
  
  copyinstr(uap->name,kern_name,256,NULL);
  copyinstr(uap->message,kern_msg,300,NULL);
  
  cursor = Container_head;
  
  mtx_lock(&mutex);
  
  while( cursor != NULL)
  {
    if(strcmp(cursor->name,kern_name) == 0)
	{
	  writenode = cursor;
	  break;
	}
	
	cursor = cursor->next;
  }
  
  if(uap->len > 255)
  {
    //mtx_unlock(&mutex);
    return EFBIG;//Error: Message too long.
  }
  
  if(writenode == NULL)
  {
    //mtx_unlock(&mutex);
    return ENOENT;//Error:Container doesn't exist. 
  }
  
  if(writenode->perms == 1 && writenode->uid != td->td_ucred->cr_ruid)
  {
   // mtx_unlock(&mutex);
    return EPERM;//Error: Operation write not allowed.
  }
  
  //sleep until message got read
  if(writenode->data != NULL && writenode->currenMsgred == 0)
  {
     //writenode->msgstate = 2;
	 
	 //addr = writenode->name;
     tsleep(writenode->write_wchan,0,NULL,0);
	  
	  //Check if the node is destroyed
     cursor = Container_head;
     while( cursor != NULL)
    {
      if(strcmp(cursor->name,writenode->name) == 0)
	  {
	   checknode = writenode;
	   break;
	  }
	
	  cursor = cursor->next;
    }
  
     //The container didn't exit,so write() should fail.
     if(checknode == NULL)
    {
      //mtx_unlock(&mutex);
      return ENOENT;
    }
	
  }
  
  writenode->data = malloc(256, M_NODE, M_NOWAIT);
  bzero(writenode->data,256);
  copystr(kern_msg,writenode->data,uap->len,NULL);
  //strncpy(writenode->data,kern_msg,uap->len);
  wakeup(writenode->read_wchan);
  /*
  if(writenode->msgstate == 4)
  {
    //addr = writenode->name;
    
  }
  */
  writenode->currenMsgred = 0;
  //writenode->msgstate = 3;
  
  mtx_unlock(&mutex);
  
  return 0;
}

int	
sys_read_container(struct thread *td, struct read_container_args *uap)
{
  struct node *cursor;//traverse the linked list
  struct node *readnode = NULL;  
  char *kern_name;
  //char *addr;
  
  kern_name = (char *)malloc(256, M_NODE, M_NOWAIT);
  bzero(kern_name,256);
  
  copyinstr(uap->name,kern_name,256,NULL);
 
  cursor = Container_head;
  while( cursor != NULL)
  {
    if(strcmp(cursor->name,kern_name) == 0 && cursor->ctid == td->td_proc->p_containerid)
	{
	  readnode = cursor;
	  break;
	}
	
	cursor = cursor->next;
  }
  
  if(readnode == NULL)
  {
    return ENOENT;
  }
  
  if(readnode->data == NULL)
  {
    readnode->msgstate = 4;
	
	//addr = readnode->name;
    tsleep(readnode->read_wchan,0,NULL,0);
  }
  
  mtx_lock(&mutex);
  
  if(strlen(readnode->data) <= uap->len)
  {
    copyout(readnode->data,uap->message,256);
	td->td_retval[0] = strlen(readnode->data);
  }
  else
  {
    copyout(readnode->data,uap->message,uap->len);
	td->td_retval[0] = uap->len;
  }
  
  if(readnode->currenMsgred == 0 )
  {
    //addr = &(readnode->name);
	readnode->currenMsgred = 1;
  }  
  
  wakeup(readnode->write_wchan);
  
  mtx_unlock(&mutex);
 
  return 0;
}
