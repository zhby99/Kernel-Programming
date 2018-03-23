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
    struct task_struct *linux_task;
    struct timer_list wakeup_timer;
    unsigned int pid;
    unsigned int cpu_time;
    unsigned int period;
    unsigned int state;
    unsigned long next_period;
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
	mp2_task_struct *tmp;
	unsigned long flags;
	spin_lock_irqsave(&mp2_lock,flags);
	list_for_each_entry(tmp,&task_list, list){
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

static int set_scheduler(struct task_struct *task, int method, int priority){
	struct sched_param sparam;
	sparam.sched_priority = priority;
	return sched_setscheduler(task, method, &sparam);
}

mp2_task_struct* highestPrior(void){
	struct list_head* pos;
	mp2_task_struct *tmp, *sel = NULL;
	unsigned long flags;
	unsigned int invPrior = 0xffffffff;

	spin_lock_irqsave(&mp2_lock,flags);
	list_for_each(pos,&task_list){
		tmp = list_entry(pos, mp2_task_struct,list);
		if(tmp->period < invPrior && tmp->state == READY){
			sel = tmp;
			invPrior = tmp->period;
		}
	}
	spin_unlock_irqrestore(&mp2_lock,flags);
	return sel;
}

int dispatch_thread(void *data){
	while(1) {
		mp2_task_struct *sel;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		if (kthread_should_stop()) {
            return 0;
        }
        mutex_lock_interruptible(&task_mutex);
		sel = highestPrior();
		if(sel == NULL) {
			if(current_mp2_task){
				current_mp2_task->state = READY;
				set_scheduler(current_mp2_task->linux_task, SCHED_NORMAL, 0);
			}
		}
		else if((current_mp2_task && current_mp2_task->period > sel->period) || !current_mp2_task){
			if(current_mp2_task) {
				current_mp2_task->state = READY;
				//for the old running task (preempted task), the dispatching thread should execute the following code:
				set_scheduler(current_mp2_task->linux_task, SCHED_NORMAL, 0);
      			set_task_state(current_mp2_task->linux_task, TASK_UNINTERRUPTIBLE);
			}
			sel->state = RUNNING;

			//For the preempting task
			set_scheduler(sel->linux_task, SCHED_FIFO, 99);
	      	current_mp2_task = sel;
			wake_up_process(sel->linux_task);
		}
		mutex_unlock(&task_mutex);
	}
	printk("dispatcher exit.\n");
	return 0;
}

void timer_handler(unsigned long task_pid){
    mp2_task_struct *wakeup_task, *tmp;
	unsigned long flags;
	spin_lock_irqsave(&mp2_lock, flags);
	list_for_each_entry(tmp, &task_list, list){
		if(tmp->pid == task_pid){
			wakeup_task = tmp;
		}
	}

	if(!wakeup_task || wakeup_task->linux_task == NULL || wakeup_task->state != SLEEPING) {
		spin_unlock_irqrestore(&mp2_lock, flags);
		printk("timer of task %u failed with error.\n", task_pid);
		return;
	}
	wakeup_task->state = READY;
	spin_unlock_irqrestore(&mp2_lock, flags);
	wake_up_process(dispatcher);
	return;
}

// void yielding(unsigned int pid){
// 	unsigned long flags;
//     mp2_task_struct *sel, *tmp;
// 	spin_lock_irqsave(&mp2_lock, flags);
//     list_for_each_entry(tmp, &task_list, list){
// 		if(tmp->pid == pid){
// 			sel = tmp;
// 		}
// 	}
// 	spin_unlock_irqrestore(&mp2_lock, flags);
//
// 	if(jiffies < sel->next_period){
// 			sel->state = SLEEPING;
// 			mod_timer(&(sel->wakeup_timer),sel->next_period);
// 			mutex_lock_interruptible(&task_mutex);
//     		current_mp2_task = NULL;
//     		mutex_unlock(&task_mutex);
//             // wake up scheduler
//    			wake_up_process(dispatcher);
//             // sleep
//     		set_task_state(sel->linux_task, TASK_UNINTERRUPTIBLE);
//     		schedule();
// 	}
// 	sel->next_period += msecs_to_jiffies(sel->period);
// 	printk("task %u finished!\n",pid);
//
// }

void yielding(unsigned int pid)
{
	unsigned long flags;
    mp2_task_struct *tmp, *tmp1;

	struct list_head *pos;

	spin_lock_irqsave(&mp2_lock, flags);
    list_for_each(pos,&task_list){
		tmp1 = list_entry(pos, mp2_task_struct,list);
		if(tmp1->pid == pid){
			tmp = tmp1;
		}
	}
	spin_unlock_irqrestore(&mp2_lock, flags);

	if(jiffies < tmp->next_period){
			tmp->state = SLEEPING;
			mod_timer(&(tmp->wakeup_timer),tmp->next_period);
			mutex_lock_interruptible(&task_mutex);
    		current_mp2_task = NULL;
    		mutex_unlock(&task_mutex);
    // wake up scheduler
   			wake_up_process(dispatcher);
    // sleep
    		set_task_state(tmp->linux_task, TASK_UNINTERRUPTIBLE);
    		schedule();
	}
	tmp->next_period += msecs_to_jiffies(tmp->period);
	printk("task %u finished!\n",pid);

}

int admission_control(unsigned int cpu_time, unsigned int period){
	unsigned long flags;
	mp2_task_struct *tmp;

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

   struct list_head *pos, *n;
   mp2_task_struct* tmp;
   list_for_each_safe(pos,n,&task_list){
       tmp = list_entry(pos, mp2_task_struct,list);
       list_del(&tmp->list);
       del_timer(&tmp->wakeup_timer);
       kmem_cache_free(mp2_cache,pos);
   }

   kmem_cache_destroy(mp2_cache);
   list_del(&task_list);

   remove_proc_entry(FILE, proc_directory);
   remove_proc_entry(DIRECTORY, NULL);
   printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp2_init);
module_exit(mp2_exit);
