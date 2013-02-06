#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/sysproto.h>
#include <sys/syscall.h>
#include <sys/fcntl.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/dirent.h>
#include <sys/extattr.h>


#include "../../../local/myfs/myfs_quota.h"
#include "../../../local/myfs/myfs_inode.h"
#include "../../../local/myfs/myfs_dir.h"
#include "../../../local/myfs/myfs_extattr.h"
#include "../../../local/myfs/myfs_ufsmount.h"
#include "../../../local/myfs/myfs_ufs_extern.h"
#include "../../../local/myfs/myfs_ffs_extern.h"



#ifndef _SYS_SYSPROTO_H_

struct setacl_args {

       char *name;
     int type;//0-user,1-group.
	   int idnum;//user id number or group id number.
	   int perms;//"rwx"
};

struct clearacl_args {

       char *name;
	   int type;//0-user,1-group.
	   int idnum;//user id number or group id number.
	  
};

struct getacl_args {

       char *name;
	   int type;//0-user,1-group.
	   int idnum;//user id number or group id number.
	  
};

#endif


int entry_find(struct myfs_ufs2_dinode *dip,int type,int idnum);


int
sys_setacl(struct thread *td, struct setacl_args *uap)
{
	int error;
	struct nameidata nd;
	int i;
	int index = -1;

	NDINIT(&nd, LOOKUP, FOLLOW, UIO_USERSPACE, uap->name, td);
	if ((error = namei(&nd)) != 0)
	{
	  return error;
	}
	
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (nd.ni_vp->v_op != &myfs_ffs_vnodeops2)
	{
	  vrele(nd.ni_vp);
	  uprintf("File was not in the myfs filesystem.\n");
	  return 0;
	}
	
    struct myfs_inode *ip = MYFS_VTOI(nd.ni_vp);
	
	//User entry
	if(uap->type == 0)
	{
	    if(ip->i_din2->user_cnt != 0)
		{
			for(i = 0; i < ip->i_din2->user_cnt; i++)
			{
				if( ip->i_din2->user_entry[i].idnum == uap->idnum )
				{
					index = i;
					break;
				}
			}
		}
	
		//Not exist,add the entry
		if(ip->i_din2->user_cnt == 0 || index == -1)
		{
		  
		  if(uap->idnum == 0)
		  {
		    ip->i_din2->user_entry[ip->i_din2->user_cnt].idnum = td->td_ucred->cr_ruid;
		  }		  
		  else 
		  {
		    ip->i_din2->user_entry[ip->i_din2->user_cnt].idnum = uap->idnum;
		  }
		  
		  ip->i_din2->user_entry[ip->i_din2->user_cnt].perms = uap->perms;
		  
		  ip->i_din2->user_cnt++;
		}
		//Existed,change the entry
		else if(index >= 0)
		{
		     //Check whether it's the owner to do the change.
			if(td->td_ucred->cr_ruid != ip->i_din2->di_uid  && td->td_ucred->cr_ruid != 0)
			{
				return EPERM;
			}
		
			 ip->i_din2->user_entry[index].perms = uap->perms;
		}
		
		
	}
	//Group entry
	else if(uap->type == 1)
	{
	   if(td->td_ucred->cr_rgid == uap->idnum ||  td->td_ucred->cr_ruid == 0)
	   {
		  ip->i_din2->group_entry[ip->i_din2->group_cnt].idnum = uap->idnum;
		  
		  ip->i_din2->group_entry[ip->i_din2->group_cnt].perms = uap->perms;
		  
		  ip->i_din2->group_cnt++;      
	   }
	   else
	   {
	     return EPERM;
	   }
		
	}
	
	vrele(nd.ni_vp);
	
	return 0;
}

int
sys_clearacl(struct thread *td, struct clearacl_args *uap)
{
    int error;
	struct nameidata nd;
	int index = -1;
	int i,j;

	NDINIT(&nd, LOOKUP, FOLLOW, UIO_USERSPACE, uap->name, td);
	if ((error = namei(&nd)) != 0)
		return error;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (nd.ni_vp->v_op != &myfs_ffs_vnodeops2)
	{
	  vrele(nd.ni_vp);
	  uprintf("File was not in the myfs filesystem.\n");
	  return 0;
	}
	
	struct myfs_inode *ip = MYFS_VTOI(nd.ni_vp);
	
	struct myfs_ufs2_dinode *dip = ip->i_din2;
	
	//index = entry_find(dip,uap->type,kern_name,uap->idnum);
	if(uap->type == 0)//search user entry
	{
		for(i = 0; i < dip->user_cnt; i++)
		{
			if( dip->user_entry[i].idnum == uap->idnum )
			{
				index = i;
				break;
			}
		}
		
		if(index != -1)
		{
		   dip->user_entry[index].idnum = 0;
		   dip->user_entry[index].perms = 0;
		   dip->user_cnt--;
		   
		   //Adjust the user access control list.
		   if( index != 15 )
		   {
				if(dip->user_entry[index + 1].idnum != 0)
				{
					for(i = index + 1; i <= dip->user_cnt; i++)
					{
						dip->user_entry[i-1].idnum = dip->user_entry[i].idnum;
						dip->user_entry[i-1].perms = dip->user_entry[i].perms;
					}
					
					dip->user_entry[dip->user_cnt].idnum = 0;
					dip->user_entry[dip->user_cnt].perms = 0;
					
				}  
			}
		 
		}
	
	}
   else if(uap->type == 1)//search group entry
   {
  
		for(j = 0; j < dip->group_cnt; j++)
		{
			if(dip->group_entry[j].idnum == uap->idnum)
			{
				index = j;
				break;
			}		
		}
		
		if(index != -1)
		{
			dip->group_entry[index].idnum = 0;
		    dip->group_entry[index].perms = 0;
			dip->group_cnt--;
			
			 //Adjust the group access control list.
			if(index != 15)
			{
				if(dip->group_entry[index + 1].idnum != 0)
				{
					for(j = index + 1; j <= dip->group_cnt; j++)
					{	
					  dip->group_entry[j-1].idnum = dip->group_entry[j].idnum;
					  dip->group_entry[j-1].perms = dip->group_entry[j].perms;
				    }
					
					dip->group_entry[dip->group_cnt].idnum = 0;
					dip->group_entry[dip->group_cnt].perms = 0;
				}  
			}
		}
	
	}
	
	vrele(nd.ni_vp);
	
	return 0;
}

int
sys_getacl(struct thread *td, struct getacl_args *uap)
{
    int error;
	struct nameidata nd;
	int perms = -1;
	int whetherintable = 0;
	int i,j;
	
	NDINIT(&nd, LOOKUP, FOLLOW, UIO_USERSPACE, uap->name, td);
	if ((error = namei(&nd)) != 0)
		return error;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (nd.ni_vp->v_op != &myfs_ffs_vnodeops2)
	{
	  vrele(nd.ni_vp);
	  uprintf("File was not in the myfs filesystem.\n");
	  return 0;
	}
	
	struct myfs_inode *ip = MYFS_VTOI(nd.ni_vp);
	
	if(uap->type == 0)
	{
	  for(i = 0; i < ip->i_din2->user_cnt ; i++)
	  {
	    if(ip->i_din2->user_entry[i].idnum == uap->idnum)
		{
		  whetherintable = 1;
		  break;
		}
	  }
	  
	}
	else if(uap->type == 1)
	{
	  for(j = 0; j < ip->i_din2->group_cnt ; j++)
	  {
	    if(ip->i_din2->group_entry[j].idnum == uap->idnum)
		{
		  whetherintable = 1;
		  break;
		}
	  }
	}
	
	if(whetherintable == 0 && td->td_ucred->cr_ruid != 0)
	{
	  td->td_retval[0] = -1;
	  return EPERM;
	}
	
	struct myfs_ufs2_dinode *dip = ip->i_din2;
	
	perms = entry_find(dip,uap->type,uap->idnum);
	
	if(perms == -1)
	{     
	  td->td_retval[0] = -1;
	  return ENOENT;
	}
	
	td->td_retval[0] = perms;//return the entry's permission number.
	
	vrele(nd.ni_vp);

	return 0;
}



//Search the access control list,return the permission number of the entry
int
entry_find(struct myfs_ufs2_dinode *dip,int type,int idnum)
{
  int i,j;
  
  if(type == 0)//search user entry
  {
  
    for(i = 0; i < dip->user_cnt; i++)
    {
		if( dip->user_entry[i].idnum == idnum)
		{
	       return dip->user_entry[i].perms;
		}
	}
	
  }
  else if(type == 1)//search group entry
  {
  
    for(j = 0; j < dip->group_cnt; j++)
	{
		if(dip->group_entry[j].idnum == idnum)
		{
	       return dip->group_entry[j].perms;
		}	
	}
	
  }
  
  return (-1);
  
}
