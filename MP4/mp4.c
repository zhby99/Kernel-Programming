#define pr_fmt(fmt) "cs423_mp4: " fmt

#include <linux/lsm_hooks.h>
#include <linux/security.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/err.h>
#include <uapi/linux/stat.h>
#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/binfmts.h>
#include <linux/printk.h>
#include "mp4_given.h"

// #define cred_label(X) (struct mp4_security*)((X)->security)

/**
 * get_inode_sid - Get the inode mp4 security label id
 *
 * @inode: the input inode
 *
 * @return the inode's security id if found.
 *
 */
static int get_inode_sid(struct inode *inode, struct dentry *dentry)
{
	/*
	 * Add your code here
	 * ...
	 */
	 if (!inode->i_op || !inode->i_op->getxattr) {
        return 0;
     }
	 int len, rc, sid;
	 char *context;
	 len = 100;
     context = kmalloc(len, GFP_KERNEL);
	 if (!context) {
		 return 0;
	 }
	 rc = inode->i_op->getxattr(dentry, XATTR_NAME_MP4, context, len);
	 if (rc == -ERANGE) {
		 kfree(context);
		 return 0;
	 }
	 if (rc < 0) {
		 kfree(context);
		 return 0;
	 }
     len = rc;
	 context[len] = '\0';
	 sid = __cred_ctx_to_sid(context);
	 kfree(context);
	 return sid;
}

/**
 * mp4_bprm_set_creds - Set the credentials for a new task
 *
 * @bprm: The linux binary preparation structure
 *
 * returns 0 on success.
 */
static int mp4_bprm_set_creds(struct linux_binprm *bprm)
{
	/*
	 * Add your code here
	 * ...
	 */
	 if(!bprm || !bprm->file || !bprm->cred || !bprm->cred->security) {
		 return 0;
	 }
	 if(bprm->cred_prepared == 1) {
		 return 0;
	 }
	 struct dentry *dentry = bprm->file->f_path.dentry;
	 if(!dentry) {
		 return 0;
	 }
	 struct inode *inode = d_inode(dentry);
	 if(!inode) {
	 	return 0;
	 }
	 int sid = get_inode_sid(inode, dentry);
	 if (sid == MP4_TARGET_SID) {
		 ((struct mp4_security*)(bprm->cred->security))->mp4_flags = MP4_TARGET_SID;
	 }
	 return 0;
}

/**
 * mp4_cred_alloc_blank - Allocate a blank mp4 security label
 *
 * @cred: the new credentials
 * @gfp: the atomicity of the memory allocation
 *
 */
static int mp4_cred_alloc_blank(struct cred *cred, gfp_t gfp){
	/*
	 * Add your code here
	 * ...
	 */
	 cred->security = kmalloc(sizeof(struct mp4_security), gfp);
	 ((struct mp4_security*)cred->security)->mp4_flags = MP4_NO_ACCESS;
	 return 0;
}


/**
 * mp4_cred_free - Free a created security label
 *
 * @cred: the credentials struct
 *
 */
static void mp4_cred_free(struct cred *cred)
{
	/*
	 * Add your code here
	 * ...
	 */
	 kfree(cred->security);
	 cred->security = NULL;

}

/**
 * mp4_cred_prepare - Prepare new credentials for modification
 *
 * @new: the new credentials
 * @old: the old credentials
 * @gfp: the atomicity of the memory allocation
 *
 */
static int mp4_cred_prepare(struct cred *new, const struct cred *old,
			    gfp_t gfp)
{
	mp4_cred_alloc_blank(new, gfp);
	if ((old) && (old->security)) {
		((struct mp4_security*)new->security)->mp4_flags = ((struct mp4_security*)old->security)->mp4_flags;
	}
	else {
		mp4_cred_alloc_blank(new, gfp);
	}
	return 0;
}

/**
 * mp4_inode_init_security - Set the security attribute of a newly created inode
 *
 * @inode: the newly created inode
 * @dir: the containing directory
 * @qstr: unused
 * @name: where to put the attribute name
 * @value: where to put the attribute value
 * @len: where to put the length of the attribute
 *
 * returns 0 if all goes well, -ENOMEM if no memory, -EOPNOTSUPP to skip
 *
 */
static int mp4_inode_init_security(struct inode *inode, struct inode *dir,
				   const struct qstr *qstr,
				   const char **name, void **value, size_t *len)
{
	/*
	 * Add your code here
	 * ...
	 */
	 if (!current_cred() || current_cred()->security) {
		 return -EOPNOTSUPP;
	 }
	 if (((struct mp4_security*)current_cred()->security)->mp4_flags == MP4_TARGET_SID) {
		 *name = (char *)kmalloc(strlen(XATTR_MP4_SUFFIX) + 1, GFP_KERNEL);
		 strcpy(*name, XATTR_MP4_SUFFIX);
		 if (S_ISDIR(inode->i_mode)) {
			 *value = (char *)kmalloc(strlen("dir-write") + 1, GFP_KERNEL);
			 strcpy(*value, "dir-write");
		 }
		 else {
			 *value = (char *)kmalloc(strlen("read-write") + 1, GFP_KERNEL);
			 strcpy(*value, "read-write");
		 }
		 *len = strlen(*value);
	 }
	 return 0;
}

/**
 * mp4_has_permission - Check if subject has permission to an object
 *
 * @ssid: the subject's security id
 * @osid: the object's security id
 * @mask: the operation mask
 *
 * returns 0 is access granter, -EACCES otherwise
 *
 */
static int mp4_has_permission(int ssid, int osid, int mask)
{
	// other
	if (ssid == MP4_NO_ACCESS) {
		if (osid == 0) {
			if (mask & MAY_ACCESS) {
				return 0;
			}
			else {
				return -EACCES;
			}
		}
		else if (osid == 1 || osid == 2 || osid == 3) {
			if (mask & MAY_READ) {
				return 0;
			}
			else {
				return -EACCES;
			}
		}
		else if (osid == 4) {
			if (mask & (MAY_READ | MAY_EXEC)) {
				return 0;
			}
			else {
				return -EACCES;
			}
		}
		else if (osid == 5 || osid == 6) {
			return 0;
		}
		else {
			return 0;
		}
	}
	else if (ssid == MP4_TARGET_SID) {
		if (osid == 0) {
				return -EACCES;
		}

		else if (osid == 1) {
			if (mask & MAY_READ) {
				return 0;
			}
			else {
				return -EACCES;
			}
		}
		else if (osid == 2) {
			if (mask & (MAY_READ | MAY_WRITE | MAY_APPEND)) {
				return 0;
			}
			else {
				return -EACCES;
			}
		}
		else if (osid == 3) {
			if (mask & (MAY_WRITE | MAY_APPEND)) {
				return 0;
			}
			else {
				return -EACCES;
			}
		}
		else if (osid == 4) {
			if (mask & (MAY_READ | MAY_EXEC)) {
				return 0;
			}
			else {
				return -EACCES;
			}
		}
		else if (osid == 5) {
			if (mask & (MAY_READ | MAY_EXEC | MAY_ACCESS)) {
				return 0;
			}
			else {
				return -EACCES;
			}
		}
		else if (osid == 6) {
			if (mask & (MAY_OPEN | MAY_CHDIR | MAY_READ | MAY_EXEC | MAY_ACCESS)) {
				return 0;
			}
			else {
				return -EACCES;
			}
		}
		else {
			return 0;
		}
	}
	else {
		return 0;
	}
	return 0;
}

/**
 * mp4_inode_permission - Check permission for an inode being opened
 *
 * @inode: the inode in question
 * @mask: the access requested
 *
 * This is the important access check hook
 *
 * returns 0 if access is granted, -EACCES otherwise
 *
 */
static int mp4_inode_permission(struct inode *inode, int mask)
{
	/*
	 * Add your code here
	 * ...
	 */
	 if (!inode) {
		 return -EACCES;
	 }
	 struct dentry *dentry;
	 dentry = d_find_alias(inode);
	 if (!dentry) {
		 dput(dentry);
		 return 0;
	 }
	 char *buf = (char *)kmalloc(4096, GFP_KERNEL);
	 if (!buf) {
		 dput(dentry);
		 return 0;
	 }

	 char *ret = dentry_path_raw(dentry, buf, 4096);
	 if(IS_ERR(ret)){
		 kfree(buf);
		 buf = NULL;
		 dput(dentry);
		 return 0;
	 }
	 if(mp4_should_skip_path(ret)) {
		 if(printk_ratelimit()) {
			 pr_info("skip this path : %s\n", ret);
		 }
		 kfree(buf);
		 buf = NULL;
		 dput(dentry);
		 return 0;
	 }
	 kfree(buf);
	 buf = NULL;
	 if (!current_cred() || !current_cred()->security) {
		 dput(dentry);
		 return 0;
	 }
	 int ssid = ((struct mp4_security*)current_cred()->security)->mp4_flags;
	 int osid = get_inode_sid(inode, dentry);
	 if (ssid != MP4_TARGET_SID && S_ISDIR(inode->i_mode)){
		 dput(dentry);
		 return 0;
	 }
	 dput(dentry);
	 if(printk_ratelimit()) {
	 	 pr_info("ssid: %d, osid: %d, mask: %d\n", ssid, osid, mask);
	 }
	 int permission = mp4_has_permission(ssid, osid, mask);
	 if (permission==0) {
	 	if(printk_ratelimit()) {
			 pr_info("Accept! ssid: %d, osid: %d, mask: %d\n", ssid, osid, mask);
		}
	 }
	 else {
		 if(printk_ratelimit()) {
			 pr_info("Denied! ssid: %d, osid: %d, mask: %d\n", ssid, osid, mask);
		 }
	 }
	 return permission;
}


/*
 * This is the list of hooks that we will using for our security module.
 */
static struct security_hook_list mp4_hooks[] = {
	/*
	 * inode function to assign a label and to check permission
	 */
	LSM_HOOK_INIT(inode_init_security, mp4_inode_init_security),
	LSM_HOOK_INIT(inode_permission, mp4_inode_permission),

	/*
	 * setting the credentials subjective security label when laucnhing a
	 * binary
	 */
	LSM_HOOK_INIT(bprm_set_creds, mp4_bprm_set_creds),

	/* credentials handling and preparation */
	LSM_HOOK_INIT(cred_alloc_blank, mp4_cred_alloc_blank),
	LSM_HOOK_INIT(cred_free, mp4_cred_free),
	LSM_HOOK_INIT(cred_prepare, mp4_cred_prepare)
};

static __init int mp4_init(void)
{
	/*
	 * check if mp4 lsm is enabled with boot parameters
	 */
	if (!security_module_enable("mp4"))
		return 0;

	pr_info("mp4 LSM initializing..");

	/*
	 * Register the mp4 hooks with lsm
	 */
	security_add_hooks(mp4_hooks, ARRAY_SIZE(mp4_hooks));

	return 0;
}

/*
 * early registration with the kernel
 */
security_initcall(mp4_init);
