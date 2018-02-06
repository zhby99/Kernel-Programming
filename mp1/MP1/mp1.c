#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include "mp1_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_ID");
MODULE_DESCRIPTION("CS-423 MP1");

#define DEBUG 1
#define DIRECTORY   "mp1"
#define FILE        "status"
#define _MAX_SIZE   1024

/**
 * The buffer used to store character for this module
 */
static char procfs_buffer[_MAX_SIZE];

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

LIST_HEAD(mp1_list);

struct proc_dir_entry *proc_directory, *proc_file;

/**
 * The function is called when /proc file is read
 */
int read_proc(struct file *file ,char *buf, size_t count, loff_t *offp ) {
    procfs_buffer_size = 0;
    process_list *tmp;
    list_for_each_entry(tmp, &mp1_list, list) {
        procfs_buffer_size += sprintf(procfs_buffer + procfs_buffer_size, "%u: %u\n", tmp->pid, 0);
    }
    procfs_buffer[procfs_buffer_size] = '\0';
    copy_to_user(buf, procfs_buffer, procfs_buffer_size);
    return procfs_buffer_size;
}

/**
 * This function is called with the /proc file is written
 */
int write_proc(struct file *filp,const char *buf, size_t count, loff_t *offp){
    process_list *tmp = kmalloc(sizeof(process_list), GFP_KERNEL);
    INIT_LIST_HEAD(&(tmp->list));
    copy_from_user(procfs_buffer, buf, count);
    procfs_buffer[count] = '\0';
    sscanf(buf, "%u", &tmp->pid);
    tmp->cpu_time = 0;
    list_add(&(tmp->list), &mp1_list);
    return count;
}

static struct file_operations mp1_fops = {
    .read = read_proc,
    .write = write_proc
};



// mp1_init - Called when module is loaded
int __init mp1_init(void)
{
   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE LOADING\n");
   #endif
   // Insert your code here ...
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
   printk(KERN_ALERT "MP1 MODULE LOADED\n");
   return 0;
}

// mp1_exit - Called when module is unloaded
void __exit mp1_exit(void)
{
   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE UNLOADING\n");
   #endif
   // Insert your code here ...
   remove_proc_entry(FILE, proc_directory);
   remove_proc_entry(DIRECTORY, NULL);
   printk(KERN_ALERT "MP1 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp1_init);
module_exit(mp1_exit);
