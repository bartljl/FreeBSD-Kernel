/*
 * string_module.c - Device driver module for string device.
 * Written by Jialong Li.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/libkern.h>
#include <sys/ioccom.h>
#include <sys/filio.h>
#include <sys/sockio.h>
#include <sys/ttycom.h>
#include <sys/ctype.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>



static d_open_t  	string_open;
static d_close_t	string_close;
static d_read_t		string_read;
static d_write_t	string_write;
static d_ioctl_t	string_ioctl;

static struct cdev *sdev;
struct mtx mutex;

MTX_SYSINIT(mutex,&mutex,"string device mutex", MTX_DEF);

char *message = "Hello World.";
int upper_case_only = 0;

static struct cdevsw string_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	string_open,
	.d_close =	string_close,
	.d_read =	string_read,
	.d_write =	string_write,
	.d_ioctl =	string_ioctl,
	.d_name =	"string",
};


static int
string_open(struct cdev *dev, int flag, int mode, struct thread *td)
{
	return(0);
}

static int
string_close(struct cdev *dev, int flag, int mode, struct thread *td)
{
	return(0);
}

static int
string_read(struct cdev *dev, struct uio *uio, int flags)
{
  	char *ptr;
	char Msg[50];
	char tmp[50];

	int error;
	
	mtx_lock(&mutex);
	
	if(upper_case_only == 1)
	{
	   copystr(message,tmp,50,NULL);
	 
	   int i = 0;
	   while(tmp[i] != 0)
	   {
	     tmp[i] = toupper(tmp[i]);
	     i++;
	   }
		
		copystr(tmp,Msg,50,NULL);
	}
	else if(upper_case_only == 0)
	{
	   copystr(message,Msg,50,NULL);
	}
	
	if(uio->uio_offset >= strlen(Msg)) 
	{
	   //uio->uio_offset = 0;
	   mtx_unlock(&mutex);
	   return 0;
	}
	  
	ptr = Msg;
	ptr += uio->uio_offset;
	
	error = uiomove(ptr,strlen(ptr) + 1,uio);
	mtx_unlock(&mutex);
	  
	return(error);
}

static int
string_write(struct cdev *dev, struct uio *uio, int flags)
{
	int error = 0;
   
   	mtx_lock(&mutex);

	if(strlen(uio->uio_iov->iov_base) > 50)
	{
	  mtx_unlock(&mutex);
	  return EFBIG;
	}
	
	bzero(message,50);
	error = copyin(uio->uio_iov->iov_base,message,uio->uio_iov->iov_len);
	uio->uio_offset = 0;
	
	mtx_unlock(&mutex);

	return(error);
}

static int
string_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int flag, struct thread *td)
{	
	upper_case_only = 1;
	return(0);
}

static int
string_load(module_t mod, int what, void *arg)
{
	int error = 0;
	
	switch(what) {
		case MOD_LOAD:
			sdev = make_dev(&string_cdevsw, 0, UID_ROOT, GID_WHEEL,
				0666, "string");
			uprintf("String device load!\n");
			break;
		case MOD_UNLOAD:
		    uprintf("String device unload!\n");
			destroy_dev(sdev);
			break;
		default:
			error = EINVAL;
			break;
	}
	return(error);
}

DEV_MODULE(string_module, string_load, NULL);
