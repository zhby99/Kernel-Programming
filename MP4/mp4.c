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
#include "mp4_given.h"

#define cred_label(X) (struct mp4_security*)((X)->security)

static int inode_init_with_dentry(struct dentry *dentry, struct inode *inode){
	int len, rc, sid;
	char *context;
	len = 100;
    context = kmalloc(len, GFP_KERNEL);
	rc = inode->i_op->getxattr(dentry, XATTR_NAME_MP4, context, len);
    len = rc;
	dput(dentry);
	context[len] = '\0';
	sid = __cred_ctx_to_sid(context);
	return sid;
}

/**
 * get_inode_sid - Get the inode mp4 security label id
 *
 * @inode: the input inode
 *
 * @return the inode's security id if found.
 *
 */
static int get_inode_sid(struct inode *inode)
{
	/*
	 * Add your code here
	 * ...
	 */
	 struct dentry *dentry;
	 dentry = d_find_alias(inode);
	 if (!dentry) {
		 return -1;
	 }
	 return inode_init_with_dentry(dentry, inode);
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
	 struct inode *inode = bprm->file->f_inode;
	 int sid = get_inode_sid(inode);
	 if (sid == -1) {
		 return 0;
	 }
	 if (sid == MP4_TARGET_SID) {
		 cred_label(bprm->cred)->mp4_flags = MP4_TARGET_SID;
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
	 cred_label(cred) = (struct mp4_security*)kmalloc(sizeof(struct mp4_security), gfp);
	 cred_label(cred)->mp4_flags = MP4_NO_ACCESS;
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
	 cred_label(cred) = NULL;
	 kfree(cred_label(cred));
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
	if (cred_label(old)) {
		cred_label(new)->mp4_flags = cred_label(old)->mp4_flags;
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
	 if (!current_cred()) {
		 return -EOPNOTSUPP;
	 }
	 if (!cred_label(current_cred())) {
		 return -EOPNOTSUPP;
	 }
	 if (cred_label(current_cred())->mp4_flags == MP4_TARGET_SID) {
		 *name = (char *)kmalloc(strlen(XATTR_NAME_MP4) + 1, GFP_KERNEL);
		 strcpy(*name, XATTR_NAME_MP4);
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
	if (ssid == 0) {
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
		else if (osid == 5) {
			if (mask & (MAY_READ | MAY_EXEC | MAY_ACCESS)) {
				return 0;
			}
			else {
				return -EACCES;
			}
		}
		else {
			return -EACCES;
		}
	}
	else if (ssid == 7) {
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
			return -EACCES;
		}
	}
	else {
		return -EACCES;
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
	 struct dentry *dentry;
	 dentry = d_find_alias(inode);
	 if (!dentry) {
		 return -EACCES;
	 }
	 char *buf = kmalloc(100, GFP_KERNEL);
	 dentry_path(dentry, buf, 100);
	 if(mp4_should_skip_path(buf)) {
		 kfree(buf);
		 return -EACCES;
	 }
	 if (!current_cred()) {
		 return -EACCES;
	 }
	 if (!cred_label(current_cred())) {
		 return -EACCES;
	 }

	 int ssid = cred_label(current_cred())->mp4_flags;
	 int osid = get_inode_sid(inode);
	 return mp4_has_permission(ssid, osid, mask);
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
