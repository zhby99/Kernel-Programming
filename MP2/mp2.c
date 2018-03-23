#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include "mp2_given.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CS-423 MP2");

#define DEBUG 1
#define DIRECTORY   "mp2"
#define FILE        "status"
#define _MAX_SIZE   1024
#define T_INTERVAL  5000

static spinlock_t mp2_lock;
static struct kmem_cache *mp2_cache;

typedef struct {
    struct task_struct *linux_task;
    struct timer_list wakeup_timer;
    struct list_head list;
    unsigned int pid;
    unsigned int cpu_time;
    unsigned int period;
    unsigned int state;
    unsigned long next_period;
} mp2_task_struct;


static const struct file_operations mp2_fops = {
    .read    = mp2_read,
    .write   = mp2_write
};

/**
 * The function is called when /proc file is read
 */
static ssize_t mp2_read(struct file *file, char __user *buffer, size_t count, loff_t *data){
    ssize_t copied = 0;
    char *buf = (char *)kmalloc(count, GFP_KERNEL);
    mp2_task_struct *tmp;
	struct list_head *pos;
    unsigned long flags;
    spin_lock_irqsave(&mp2_lock,flags);
    list_for_each_entry(tmp, &task_list, list){
		copied += sprintf(buf+copied,"%u %u %u %u\n",tmp->pid,tmp->period,tmp->cpu_time, tmp->state);
	}
    spin_unlock_irqrestore(&mp2_lock,flags);
    buf[copied] = '\0';
	if(copy_to_user(buffer, buf, copied)){
		kfree(buf);
		printk("Error in read\n");
		return -EINVAL;
	}
    kfree(buf);
    return copied;
}

/**
 * This function is called with the /proc file is written
 */
static ssize_t mp2_write(struct file *file, const char __user *buffer, size_t count, loff_t *data){
    char *buf;
    char command;
	unsigned int period;
	unsigned long cpu_time;
	unsigned int pid;
    buf = (char *)kmalloc(count + 1, GFP_KERNEL);
    if(copy_from_user(buf,buffer, count)){
		kfree(buf);
		return -EFAULT;
	}
    buf[count] = '\0';
    command = buf[0];

    // use switch for different kinds of input
    switch (command) {
        case 'R':
            sscanf(buf,"%c, %u, %u, %lu",&command,&pid,&period,&cpu_time);
            registration(pid,period,cpu_time);
            break;
        case 'Y':
            sscanf(buf,"%c, %u",&command,&pid);
            yield(pid);
            break;
        case 'D':
            sscanf(buf, "%c, %u", &command, &pid);
            de_registration(pid);
            break;
        default:
            printk("invalid command\n");
            break;
    }
    kfree(buf);
    return count;
}

/**
 * Doing registration here.
 */
void registration(unsigned int pid,unsigned long period, unsigned long cpu_time){
	unsigned long flags;
	mp2_task_struct *tmp = (mp2_task_struct *)kmem_cache_alloc(mp2_cache, GFP_KERNEL);
	tmp->pid = pid;
	tmp->period = period;
	tmp->cpu_time = cpu_time;
	tmp->next_period = jiffies + msecs_to_jiffies(period);
	tmp->linux_task = find_task_by_pid(pid);
	tmp->state = SLEEPING;
	INIT_LIST_HEAD(&(tmp->list));
	setup_timer(&(tmp->wakeup_timer),timer_handler, pid);
	if( !admission_control(cpu_time, period) || !tmp->linux_task) {

		del_timer(&(tmp->wakeup_timer));
		kmem_cache_free(mp2_cache, tmp);
		printk("admission control failed or cannot find task by pid!");
		return;
	}

	spin_lock_irqsave(&mp2_lock,flags);
	list_add(&(tmp->list), &task_list);
	spin_unlock_irqrestore(&mp2_lock,flags);
	printk("task %u registered!\n",pid);
}


void de_registration(unsigned int pid){
	struct list_head *pos, *n;
	mp2_task_struct *tmp;
	unsigned long flags;
	spin_lock_irqsave(&mp2_lock,flags);
	list_for_each_safe(pos,n,&task_list){
		tmp = list_entry(pos,struct mp2_task_struct,list);
		if(tmp->pid == pid){
			list_del(&tmp->list);
			del_timer(&tmp->wakeup_timer);
			kmem_cache_free(mp2_cache,tmp);
			if(current_mp2_task == tmp){
				current_mp2_task = NULL;
				wake_up_process(dispatcher);
			}
			spin_unlock_irqrestore(&mp2_lock,flags);
			printk("task %u de-registered!\n",pid);
			return;
		}
	}
	spin_unlock_irqrestore(&mp2_lock,flags);
	printk("task %u de-registered!",pid);
}

// mp1_init - Called when module is loaded
int __init mp2_init(void)
{
   #ifdef DEBUG
   printk(KERN_ALERT "MP2 MODULE LOADING\n");
   #endif
   // Insert your code here ...
   proc_directory = proc_mkdir(DIRECTORY, NULL);
   if (!proc_directory) {
       remove_proc_entry(DIRECTORY, NULL);
       printk(KERN_ALERT "Error: Could not initialize /proc/%s\n", DIRECTORY);
       return -ENOMEM;
   }
   // create entry for FILE
   proc_file = proc_create(FILE, 0666, proc_directory, &mp2_fops);
   if (!proc_file) {
       remove_proc_entry(FILE, proc_directory);
       printk(KERN_ALERT "Error: Could not initialize /proc/%s/%s\n", DIRECTORY, FILE);
       return -ENOMEM;
   }

   mp2_cache = kmem_cache_create("mp2_task_struct",sizeof(mp2_task_struct), ARCH_MIN_TASKALIGN,SLAB_PANIC, NULL);

   setup_timer(&my_timer, my_timer_callback, 0);
   mod_timer(&my_timer, jiffies + msecs_to_jiffies(T_INTERVAL));

   my_wq = create_workqueue("my_queue");
   spin_lock_init(&mp2_lock);
   my_work = (struct work_struct *)kmalloc(sizeof(struct work_struct), GFP_KERNEL);
   INIT_WORK(my_work, my_work_function);

   printk(KERN_ALERT "MP2 MODULE LOADED\n");
   return 0;
}

// mp1_exit - Called when module is unloaded
void __exit mp2_exit(void){
   #ifdef DEBUG
   printk(KERN_ALERT "MP2 MODULE UNLOADING\n");
   #endif
   // Insert your code here ...
   remove_proc_entry(FILE, proc_directory);
   remove_proc_entry(DIRECTORY, NULL);

   del_timer_sync(&my_timer);
   process_list *tmp, *n;
   list_for_each_entry_safe(tmp, n, &mp1_list, list) {
       list_del(&tmp->list);
       kfree(tmp);
   }
   flush_workqueue(my_wq);
   destroy_workqueue(my_wq);

   kmem_cache_destroy(mp2_cache);

   kfree(my_work);
   printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp2_init);
module_exit(mp2_exit);
