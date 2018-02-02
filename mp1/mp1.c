/*

*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/list.h>

#define DIRECTORY   "mp1"
#define FILE        "status"
#define _MAX_SIZE   1024

/**
 * The buffer used to store character for this module
 */
static char procfs_buffer[PROCFS_MAX_SIZE];

/**
 * The size of the buffer
 */
static unsigned long procfs_buffer_size = 0;

/*
    customed struct to store the process's info
*/
typedef struct {
    struct list_head list;
    unsigned int pid;
    unsigned long cpu_time;
} process_list;

struct proc_dir_entry *proc_directory, *proc_file;

static struct file_operations mp1_fops = {
    .read = read_proc,
    .write = write_proc
};

// The following 2 methods copied from http://tldp.org/LDP/lkmpg/2.6/html/x769.html;

/**
 * The function is called when /proc file is read
 */
int read_proc(char *buffer, char **buffer_location, off_t offset, int buffer_length, int *eof, void *data){
	int ret;
	printk(KERN_INFO "read_proc (/proc/%s/%s) called\n", DIRECTORY, FILE);
	if (offset > 0) {
		/* we have finished to read, return 0 */
		ret  = 0;
	} else {
		//memcpy(buffer, procfs_buffer, procfs_buffer_size);
        copy_to_user(buffer, buf, copied);
		ret = procfs_buffer_size;
	}

	return ret;
}

/**
 * This function is called with the /proc file is written
 */
int read_proc(struct file *filp,char *buf,size_t count,loff_t *offp ) {
    if(count>temp){
        count=temp;
    }
    temp=temp-count;
    copy_to_user(buf,msg, count);
    if(count==0){
        temp=len;
    }
    return count;
}

int write_proc(struct file *filp,const char *buf,size_t count,loff_t *offp){
    copy_from_user(msg,buf,count);
    len=count;
    temp=len;
    return count;
}


/*
    initial module, create Proc Filesystem entries(i.e /proc/mp1/ and /proc/mp1/status)
*/
int init_module(void) {
    // create entry for directory
    proc_directory = proc_mkdir(DIRECTORY, NULL);
    if (!proc_directory) {
        remove_proc_entry(DIRECTORY, NULL);
        printk(KERN_ALERT "Error: Could not initialize /proc/%s\n", DIRECTORY);
        return -ENOMEM;
    }
    // create entry for FILE
    proc_file = proc_create(FILE, 0666, proc_directory, &mp1_fops);
    if (!proc_file) {
        remove_proc_entry(FILE, proc_directory);
        printk(KERN_ALERT "Error: Could not initialize /proc/%s/%s\n", DIRECTORY, FILE);
        return -ENOMEM;
    }
    return 0;
}

void cleanup_module(void) {
    remove_proc_entry(FILE, proc_directory);
    remove_proc_entry(DIRECTORY, NULL);
}
