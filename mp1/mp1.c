/*

*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

#define DIRECTORY   "mp1"
#define FILE        "status"

struct proc_dir_entry *proc_directory, *proc_file;

static struct file_operations mp1_fops = {
    .read: read_proc,
    .write: write_proc
};

/*
initial module, create Proc Filesystem entries(i.e /proc/mp1/ and /proc/mp1/status)
*/
static int __int mp1_init(void) {
    // create entry for directory
    proc_directory = proc_mkdir(DIRECTORY, NULL);
    if (!proc_directory) {
        remove_proc_entry(DIRECTORY, NULL);
        printk(KERN_ALERT "Error: Could not initialize /proc/%s\n", DIRECTORY);
        return -ENOMEM;
    }
    // create entry for FILE
    proc_file = create_proc_entry(FILE, 0666, proc_directory);
    if (!proc_file) {
        remove_proc_entry(FILE, proc_directory);
        printk(KERN_ALERT "Error: Could not initialize /proc/%s/%s\n", DIRECTORY, FILE);
        return -ENOMEM;
    }
    proc_file->proc_fops = &mp1_fops;
    return 0;
}

static void __exit mp1_exit(void) {
    remove_proc_entry(FILE, proc_directory);
    remove_proc_entry(DIRECTORY, NULL);
}

module_init(mp1_init);
module_exit(mp1_exit);
