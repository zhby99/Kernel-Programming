#define LINUX
#include "mp2_given.h"
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CS-423 MP2");

#define DEBUG 1
#define DIRECTORY   "mp2"
#define FILE        "status"
#define _MAX_SIZE   1024
#define T_INTERVAL  5000
#define SLEEPING    0
#define READY       1
#define RUNNING    2

void registration(unsigned int, unsigned long, unsigned long);
void de_registration(unsigned int);
int dispatch_thread(void *);
void timer_handler(unsigned long);
void yielding(unsigned int);
int admission_control(unsigned int, unsigned int);




DEFINE_MUTEX(task_mutex);
static spinlock_t mp2_lock;
static struct kmem_cache *mp2_cache;
static struct task_struct *dispatcher;

struct proc_dir_entry *proc_directory, *proc_file;

typedef struct {
    struct list_head list;
    struct task_struct *task;
    struct timer_list wakeup_timer;
    unsigned int pid;
    unsigned int cpu_time;
    unsigned int period;
    unsigned int state;
    unsigned long next;
} mp2_task_struct;

LIST_HEAD(task_list);

mp2_task_struct *current_mp2_task = NULL;



/**
 * The function is called when /proc file is read
 */
ssize_t mp2_read(struct file *file, char __user *buffer, size_t count, loff_t *data){
    ssize_t copied = 0;
    char *buf = (char *)kmalloc(count, GFP_KERNEL);
    mp2_task_struct *tmp;
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
ssize_t mp2_write(struct file *file, const char __user *buffer, size_t count, loff_t *data){
    char *buf;
    char command;
	unsigned int period;
	unsigned long cpu_time;
	unsigned int pid;
    buf = (char *)kmalloc(count + 1, GFP_KERNEL);
    // if(copy_from_user(buf,buffer, count)){
	// 	kfree(buf);
	// 	return -EFAULT;
	// }
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
            yielding(pid);
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

static struct file_operations mp2_fops = {
    .read    = mp2_read,
    .write   = mp2_write
};

// helper function!
mp2_task_struct *get_task_by_pid(int pid){
    mp2_task_struct *tmp;
    list_for_each_entry(tmp, &task_list, list) {
        if (tmp->pid == pid) {
            return tmp;
        }
    }
    return NULL;
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
	tmp->next = jiffies + msecs_to_jiffies(period);
	tmp->task = find_task_by_pid(pid);
	tmp->state = SLEEPING;
    setup_timer(&(tmp->wakeup_timer),timer_handler, pid);
	INIT_LIST_HEAD(&(tmp->list));

    // cannot find this task or cannot pass admission control.
	if( !admission_control(cpu_time, period) || !tmp->task) {
		del_timer(&(tmp->wakeup_timer));
		kmem_cache_free(mp2_cache, tmp);
		printk("cannot find this task or cannot pass admission control!");
		return;
	}

	spin_lock_irqsave(&mp2_lock,flags);
	list_add(&(tmp->list), &task_list);
	spin_unlock_irqrestore(&mp2_lock,flags);
	printk("task %u registered!\n",pid);
}


// doing de registration here.
void de_registration(unsigned int pid){
	mp2_task_struct *tmp;
	unsigned long flags;
	spin_lock_irqsave(&mp2_lock,flags);

    tmp = get_task_by_pid(pid);
    if(!tmp) {
        spin_unlock_irqrestore(&mp2_lock,flags);
        return;
    }
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

void set_priority(mp2_task_struct *task, int policy, int priority){
	struct sched_param sparam;
	sparam.sched_priority = priority;
	sched_setscheduler(task->task, policy, &sparam);
}

int dispatch_thread(void *data){
	while(1) {
		mp2_task_struct *sel = NULL;
        mp2_task_struct *tmp;
        int prev = INT_MAX;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		if (kthread_should_stop()) {
            return 0;
        }
        mutex_lock_interruptible(&task_mutex);

        list_for_each_entry(tmp,&task_list, list){
    		if(tmp->period < prev && tmp->state == READY){
    			sel = tmp;
    			prev = tmp->period;
    		}
    	}
		if(sel == NULL) {
			if(current_mp2_task){
				current_mp2_task->state = READY;
				set_priority(current_mp2_task, SCHED_NORMAL, 0);
			}
		}
		else{
			if(current_mp2_task && current_mp2_task->period > sel->period) {
                printk(KERN_ALERT "PREEMPTING PID: %d\n", current_mp2_task->pid);
				current_mp2_task->state = READY;
				set_priority(current_mp2_task, SCHED_NORMAL, 0);
      			set_task_state(current_mp2_task->task, TASK_UNINTERRUPTIBLE);
			}
			sel->state = RUNNING;
			set_priority(sel, SCHED_FIFO, 99);
	      	current_mp2_task = sel;
			wake_up_process(sel->task);
		}
		mutex_unlock(&task_mutex);
	}
	printk("dispatcher exit.\n");
	return 0;
}

void timer_handler(unsigned long pid){
    mp2_task_struct *wakeup_task, *tmp;
	unsigned long flags;
	spin_lock_irqsave(&mp2_lock, flags);
	wakeup_task = get_task_by_pid(pid);

	if(!wakeup_task || wakeup_task->task == NULL || wakeup_task->state != SLEEPING) {
		spin_unlock_irqrestore(&mp2_lock, flags);
		printk("Failed with error.\n", task_pid);
		return;
	}
	wakeup_task->state = READY;
	spin_unlock_irqrestore(&mp2_lock, flags);
	wake_up_process(dispatcher);
	return;
}

void yielding(unsigned int pid){

    mp2_task_struct *sel;

    sel = get_task_by_pid(pid);

	if(jiffies < sel->next){
			sel->state = SLEEPING;
			mod_timer(&(sel->wakeup_timer),sel->next);
			mutex_lock_interruptible(&task_mutex);
    		current_mp2_task = NULL;
    		mutex_unlock(&task_mutex);
            // wake up scheduler
   			wake_up_process(dispatcher);
            // sleep
    		set_task_state(sel->task, TASK_UNINTERRUPTIBLE);
    		schedule();
	}
	sel->next += msecs_to_jiffies(sel->period);
	printk("task %u finished!\n",pid);

}

// use admission control to meet all deadlines, simply not accept new one that would lead to X.
int admission_control(unsigned int cpu_time, unsigned int period){
	unsigned long flags;
	mp2_task_struct *tmp;
    // multiply by 1000 to avoid floating point arithmetics.
	spin_lock_irqsave(&mp2_lock,flags);
	unsigned int ratio = (1000 * cpu_time) / period;
	list_for_each_entry(tmp, &task_list, list)
		ratio += (1000 * tmp->cpu_time) / tmp->period;
	spin_unlock_irqrestore(&mp2_lock,flags);
	return ratio < 693;
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

   dispatcher = kthread_run(dispatch_thread, NULL, "dispatching thread");

   spin_lock_init(&mp2_lock);

   printk(KERN_ALERT "MP2 MODULE LOADED\n");
   return 0;
}

// mp1_exit - Called when module is unloaded
void __exit mp2_exit(void){
   #ifdef DEBUG
   printk(KERN_ALERT "MP2 MODULE UNLOADING\n");
   #endif
   // Insert your code here ...

   int ret = kthread_stop(dispatcher);
   if (!ret)
       printk("Counter thread has stopped\n");
   mutex_destroy(&task_mutex);

   mp2_task_struct *entry, *temp_entry;
   spin_lock(&mp2_lock);
  //go through the list and detroy the list entry and timer inside of mp task struct
   list_for_each_entry_safe(entry, temp_entry, &task_list, list){
       list_del(&(entry->list));
       del_timer( &entry->wakeup_timer );
       kmem_cache_free(mp2_cache, entry);
   }
  //destroy allocated memory
   kmem_cache_destroy(mp2_cache);

   spin_unlock(&mp2_lock);

   list_del(&task_list);

   remove_proc_entry(FILE, proc_directory);
   remove_proc_entry(DIRECTORY, NULL);
   printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp2_init);
module_exit(mp2_exit);
