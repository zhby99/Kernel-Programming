#define pr_fmt(fmt) "cs423_mp4: " fmt

#include <linux/lsm_hooks.h>
#include <linux/security.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/binfmts.h>
#include <linux/limits.h>
#include "mp4_given.h"

#define MP4_XATTR_VALUE_MAX_SIZE 15
/*
// Mask Macros
#define MAY_EXEC		0x00000001
#define MAY_WRITE		0x00000002
#define MAY_READ		0x00000004
#define MAY_APPEND		0x00000008
#define MAY_ACCESS		0x00000010
#define MAY_OPEN		0x00000020
#define MAY_CHDIR		0x00000040
// called from RCU mode, don't block
#define MAY_NOT_BLOCK	0x00000080
*/

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
	// declarations
	struct dentry *dentry;
	char *xattr;
	//char xattr[MP4_XATTR_VALUE_MAX_SIZE];
	ssize_t res;
	int label = -1;

	// check if inode is NULL
	if (!inode) {
		if (printk_ratelimit()) pr_err("NULL inode");
		return 0;
		//return -1;
	}

	// find the appropriate dentry (dentry to inode -> many-to-one)
	dentry = d_find_alias(inode);
	if (!dentry) {
		if (printk_ratelimit()) pr_err("no dentry found for this inode");
		return 0;
		//return -1; // return -1 on failure?
	}

	// get the extended attribute
	xattr = kmalloc(MP4_XATTR_VALUE_MAX_SIZE, GFP_KERNEL);
	if (!xattr) {
		if (printk_ratelimit()) pr_err("unable to allocate space for xattr buffer");
		dput(dentry);
		return 0;
		//return -1;
	}

	if (inode->i_op && inode->i_op->getxattr) {
		res = inode->i_op->getxattr(dentry, XATTR_NAME_MP4, (void*)xattr, MP4_XATTR_VALUE_MAX_SIZE);
		//no need to check if res < 0 here, if it is, label = MP4_NO_ACCESS and only label MP4_TARGET_SID will be useful
		label = __cred_ctx_to_sid(xattr);
		//pr_info("attribute name: %s, attribute sid: %d", xattr, label);
	}

	// free heap and put the dentry structure back
	kfree(xattr);
	dput(dentry);
	return label;
}

static int mp4_cred_alloc_blank(struct cred *cred, gfp_t gfp);
/**
 * mp4_bprm_set_creds - Set the credentials for a new task
 *
 * @bprm: The linux binary preparation structure
 *
 * returns 0 on success.
 */
static int mp4_bprm_set_creds(struct linux_binprm *bprm)
{

	struct inode *inode;
	int label;

	// check if bprm is NULL
	if (!bprm) {
		if (printk_ratelimit()) pr_err("NULL bprm");
		return 0;
	}

	// check if bprm->file is NULL
	if (!(bprm->file)) {
		if (printk_ratelimit()) pr_err("NULL bprm->file");
		return 0;
	}

	// find the inode
	inode = bprm->file->f_inode;

	// get the inode mp4 security label id
	label = get_inode_sid(inode);

	// if the label is MP4_TARGET_SID, set the created task's blob to MP4_TARGET_SID
	if (label == MP4_TARGET_SID) {
		if (bprm->cred) {
			if (!(bprm->cred->security))
				mp4_cred_alloc_blank(bprm->cred, GFP_KERNEL);
			((struct mp4_security*)(bprm->cred->security))->mp4_flags = MP4_TARGET_SID;
		}
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
static int mp4_cred_alloc_blank(struct cred *cred, gfp_t gfp)
{
	// check if cred is NULL
	if (!cred) {
		if (printk_ratelimit()) pr_err("NULL cred");
		return 0;
	}

	cred->security = (struct mp4_security*)kmalloc(sizeof(struct mp4_security), gfp);
	if (!(cred->security)) return 1; // if kmalloc fails
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
	// check if cred is NULL
	if (!cred) {
		if (printk_ratelimit()) pr_err("NULL cred");
		return;
	}
	if (cred->security) kfree(cred->security);
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
	// check if new or old is NULL
	if ((!new) || (!old)) {
		if (printk_ratelimit()) pr_err("NULL new or old");
		return 0;
	}

	if (!(new->security)) {
		int res = mp4_cred_alloc_blank(new, gfp);
		if (res) {
			if (printk_ratelimit()) pr_err("failed to create a new blank credential");
			return 1;
		}
	}

	if (old->security)
		((struct mp4_security*)(new->security))->mp4_flags = ((struct mp4_security*)(old->security))->mp4_flags;

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

	char *tmp_name, *tmp_val;
	const struct cred *curr_cred;
	int task_label;

	if (!inode || !dir) {
		return -EOPNOTSUPP;
	}

	// if the task that creates the inode does not have the target sid, skip
	curr_cred = current_cred();
	// check if curr_cred is NULL
	if (!curr_cred) {
		if (printk_ratelimit()) pr_err("NULL curr_cred");
		return -EOPNOTSUPP;
	}
	task_label = ((struct mp4_security*)(curr_cred->security))->mp4_flags;
	if (task_label != MP4_TARGET_SID) {
		return -EOPNOTSUPP;
	}

	// set the allocated name suffix
	tmp_name = kstrdup(XATTR_MP4_SUFFIX, GFP_KERNEL);
	if (!tmp_name) return -ENOMEM;
	if (name) *name = tmp_name;

	// set xattr value and length
	// if the inode is a directory
	if (S_ISDIR(inode->i_mode))
		tmp_val = kstrdup("dir-write", GFP_KERNEL);
	// if the inode is a file
	else tmp_val = kstrdup("read-write", GFP_KERNEL);

	if (!tmp_val) return -ENOMEM;
	if (value) *value = tmp_val;
	if (len) *len = 11;

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
	// get the operations from mask
	bool read, write, exec, append, access, open, chdir, not_block;
	read = (bool) (mask & MAY_READ);
	write = (bool) (mask & MAY_WRITE);
	exec = (bool) (mask & MAY_EXEC);
	append = (bool) (mask & MAY_APPEND);
	access = (bool) (mask & MAY_ACCESS);
	open = (bool) (mask & MAY_OPEN);
	chdir = (bool) (mask & MAY_CHDIR);
	not_block = (bool) (mask & MAY_NOT_BLOCK);

    // MP4_NO_ACCESS - object may not be accessed by target, but may by everyone else
	if (osid == MP4_NO_ACCESS) {
		if (ssid == MP4_TARGET_SID) return -EACCES;
		else return 0;
	}

	// MP4_READ_OBJ - object may be read by anyone
	else if (osid == MP4_READ_OBJ) {
		if ((read || open || access) && (!write) && (!exec) && (!append) && (!chdir))
			return 0;
		else return -EACCES;
	}

    // MP4_READ_WRITE - object may read/written/appended by the target, but can only be read by others
	else if (osid == MP4_READ_WRITE) {
		// if the subject is the target
		if (ssid == MP4_TARGET_SID) {
			if ((read || open || access || write || append) && (!exec) && (!chdir))
				return 0;
			else return -EACCES;
		}
		else {
			if ((read || open || access) && (!write) && (!exec) && (!append) && (!chdir))
				return 0;
			else return -EACCES;
		}
	}

	// object may be written/appended by the target, but not read, and only read by others
	else if (osid ==  MP4_WRITE_OBJ) {
		if (ssid == MP4_TARGET_SID) {
			if ((open || access || write || append) && (!read) && (!exec) && (!chdir))
				return 0;
			else return -EACCES;
		}
		else {
			if ((read || open || access) && (!write) && (!exec) && (!append) && (!chdir))
				return 0;
			else return -EACCES;
		}
	}

	// object may be read and executed by all
	else if (osid == MP4_EXEC_OBJ) {
		if ((read || open || access || exec) && (!write) && (!append) && (!chdir))
			return 0;
		else
			return -EACCES;
	}

	/* NOTE: FOR DIRECTORIES, ONLY CHECK ACCESS FOR THE TARGET SID, ALL OTHER NON
 	 * TARGET PROCESSES SHOULD DEFAULT TO THE LINUX REGULAR ACCESS CONTROL
     */

	// for directories that can be read/exec/access
	else if (osid == MP4_READ_DIR) {
		if (ssid == MP4_TARGET_SID) {
			if ((read || open || access || exec) && (!write) && (!append) && (!chdir))
				return 0;
			else return -EACCES;
		}
		//else return -EACCES;
	}

	// for directory that may be modified by the target program
	else if (osid == MP4_RW_DIR) {
		if (ssid == MP4_TARGET_SID) return 0;
		//else return -EACCES;
	}
	else {
		if (printk_ratelimit()) pr_info("invalid object security id");
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

	char *path, *path_buffer;
	struct dentry *dentry;
	int ssid, osid, res;
	const struct cred *curr_cred;

	// check if the inode is NULL
	if (!inode) return 0;

	// find the path of the inode being checked
	path_buffer = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!path_buffer) {
		if (printk_ratelimit()) pr_err("unable to allocate space for path buffer");
		return 0;
	}
	dentry = d_find_alias(inode);
	if (!dentry) {
		if (printk_ratelimit()) pr_err("no dentry found for this inode");
		return 0; //return -EACCES; // return what on failure?
	}
	path = dentry_path_raw(dentry, path_buffer, PATH_MAX);

	// check if the path should be skipped
	res = mp4_should_skip_path(path);
	// if the path should be skipped
	if (res) {
		dput(dentry);
		kfree(path_buffer);
		//return -EACCES;
		return 0;
	}

	// check if the current task has the permission
	curr_cred = current_cred();
	// check if curr_cred is NULL
	if (!curr_cred) {
		if (printk_ratelimit()) pr_err("NULL curr_cred");
		return 0;
	}
	// check if curr_cred->security is NULL
	if (!(curr_cred->security)) {
		ssid = MP4_NO_ACCESS;
	}
	else ssid = ((struct mp4_security*)curr_cred->security)->mp4_flags;
	osid = get_inode_sid(inode);

	if (ssid != MP4_TARGET_SID && S_ISDIR(inode->i_mode)){
		 dput(dentry);
		 kfree(path_buffer);
		 return 0;
	 }

	res = mp4_has_permission(ssid, osid, mask);

	// if access failed, log the failed attempt to the kernel logs
	if (res) {
		if (printk_ratelimit()) pr_info("FAILED ACCESS!");
	}

	dput(dentry);
	kfree(path_buffer);

	//return res;
	return 0; // for debugging purpose
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
