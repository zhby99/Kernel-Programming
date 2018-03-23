#define LINUX
#include "mp2_given.h"
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("23");
MODULE_DESCRIPTION("CS-423 MP2");

#define DEBUG  1

struct mp2_task_struct {
    struct task_struct *linux_task;
    struct timer_list wakeup_timer;
    struct list_head list;
    unsigned int pid;
    unsigned int period;
    unsigned int cpu_time;
    unsigned int state;
    unsigned long next_period;
};
static const unsigned int SLEEPING = 0;
static const unsigned int READY = 1;
static const unsigned int RUNNING = 2;
static struct proc_dir_entry *reg_dir, *status_entry;
DEFINE_MUTEX(task_mutex);
static spinlock_t lock;
static struct task_struct *dispatcher;
static struct mp2_task_struct *current_mp2_task = NULL;
static struct kmem_cache *mp2_cache;
LIST_HEAD(task_list);


//Timer_handler(unsigned int task_pid):
//This function iterates though the proc list to find the task with pid = task_pid.
//This function is called every period of each task registered to wake up the task.
//Then it wakes up the dispatch_thread( ) function to schedule a new task for running.

void timer_handler(unsigned long task_pid)
{
	struct list_head* pos;
	struct mp2_task_struct *wakeup_task;
	struct mp2_task_struct *tmp;
	unsigned long flags;
	spin_lock_irqsave(&lock, flags);

	list_for_each(pos,&task_list){
		tmp = list_entry(pos,struct mp2_task_struct,list);
		if(tmp->pid == task_pid){
			wakeup_task = tmp;
		}
	}

	if(!wakeup_task || wakeup_task->linux_task == NULL || wakeup_task->state != SLEEPING) {
		spin_unlock_irqrestore(&lock, flags);
		printk("timer of task %u failed with error.\n", task_pid);
		return;
	}
	wakeup_task->state = READY;
	spin_unlock_irqrestore(&lock, flags);
	wake_up_process(dispatcher);
	return;
}

//admission_control( ):
//this function makes sure the CPU utilization is below a pre-set threshold.
//It will prevent
int admission_control(unsigned int cpu_time, unsigned int period)
{
	unsigned long flags;
	struct mp2_task_struct *pos;

	spin_lock_irqsave(&lock,flags);
	unsigned int ratio = (1000 * cpu_time) / period;
	list_for_each_entry(pos, &task_list, list)
		ratio += (1000 * pos->cpu_time) / pos->period;
	spin_unlock_irqrestore(&lock,flags);

	return ratio < 693;
}

static inline int set_scheduler(struct task_struct *task, int method, int priority){
	struct sched_param sparam;
	sparam.sched_priority = priority;
	return sched_setscheduler(task, method, &sparam);
}

//highestPrior( ): This function iterates through
//the proc list to find the task struct that has the
//shortest period and a READY state
struct mp2_task_struct* highestPrior(void){
	struct list_head* pos;
	struct mp2_task_struct *tmp, *sel = NULL;
	unsigned long flags;
	unsigned int invPrior = 0xffffffff;

	spin_lock_irqsave(&lock,flags);
	list_for_each(pos,&task_list){
		tmp = list_entry(pos,struct mp2_task_struct,list);
		if(tmp->period < invPrior && tmp->state == READY){
			sel = tmp;
			invPrior = tmp->period;
		}
	}
	spin_unlock_irqrestore(&lock,flags);
	return sel;
}

//dispatch_thread( ): This function takes the output
//from the highestPrior( ) function as input.
//If the input is NULL, then we just preempt the
//currently running task. Else we preempt the currently
//running task with the task with a higher priority.

static int dispatch_thread(void *data)
{
	while(true) {

		struct mp2_task_struct *sel;
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

static ssize_t mp2_read(struct file *file, char __user *buffer, size_t count, loff_t *data)
{
    ssize_t copied = 0;
    char *buf = (char *)kmalloc(count, GFP_KERNEL);
    struct mp2_task_struct *tmp;
	struct list_head* pos;

    // loop through the tasks in the list
    unsigned long flags;
    spin_lock_irqsave(&lock,flags);
    list_for_each(pos,&task_list){
		tmp = list_entry(pos,struct mp2_task_struct,list);
		copied+=sprintf(buf+copied,"%u %u %u %u\n",tmp->pid,tmp->period,tmp->cpu_time, tmp->state);
	}
    spin_unlock_irqrestore(&lock,flags);
    buf[copied] = '\0';
	if(copy_to_user(buffer, buf, copied)){
		kfree(buf);
		printk("Error in read\n");
		return -EINVAL;
	}
    kfree(buf);

    return copied;
}

//registration( ): this function initialize a task’s pid,
//period, cpu_time, next_period, task_struct , state information.
//It also initialize the task’s list_head and timer structs.
//And will add the new taks into the proc list.
void registration(unsigned int pid,unsigned long period, unsigned long cpu_time)
{
	unsigned long flags;

	//init the mp2_task_struct
	struct mp2_task_struct* tmp = (struct mp2_task_struct *)kmem_cache_alloc(mp2_cache, GFP_KERNEL);
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
		printk("admission_control or find_task_by_pid failed");
		return;
	}


	spin_lock_irqsave(&lock,flags);
	list_add(&(tmp->list), &task_list);
	spin_unlock_irqrestore(&lock,flags);

	printk("task %u registered!\n",pid);
}

//yielding( ): this function is called every time a task
//finishes its computation. It sets the task state from
//RUNNING to SLEEPING and sets the task’s timer next
//wake-up time. It also wakes up dispatcher to schedule a
//new task for running.

void yielding(unsigned int pid)
{
	unsigned long flags;
    struct mp2_task_struct *tmp, *tmp1;

	struct list_head *pos;

	spin_lock_irqsave(&lock, flags);
    list_for_each(pos,&task_list){
		tmp1 = list_entry(pos,struct mp2_task_struct,list);
		if(tmp1->pid == pid){
			tmp = tmp1;
		}
	}
	spin_unlock_irqrestore(&lock, flags);

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


//de_registration( ): this function is called at the
//kernel exit module to free space of every task in the
//proc list including the timer and the corresponding cache object.

void de_registration(unsigned int pid)
{
	struct list_head *pos, *n;
	struct mp2_task_struct *tmp;
	unsigned long flags;
	spin_lock_irqsave(&lock,flags);
	list_for_each_safe(pos,n,&task_list){
		tmp = list_entry(pos,struct mp2_task_struct,list);
		if(tmp->pid == pid){
			printk("we find you!\n");
			list_del(&tmp->list);
			del_timer(&tmp->wakeup_timer);
			kmem_cache_free(mp2_cache,tmp);
			if(current_mp2_task == tmp){
				current_mp2_task = NULL;
				wake_up_process(dispatcher);
			}
			spin_unlock_irqrestore(&lock,flags);
			printk("task %u de-registered!\n",pid);
			return;
		}
	}

	spin_unlock_irqrestore(&lock,flags);
	printk("task %u de-registered!",pid);
}



//mp2_write( ): this function is called whenever there is
//a newly-created task. The task will register itself by writing
//its pid, cpu_time, period information into the proc file system entry.
static ssize_t mp2_write(struct file *file, const char __user *buffer, size_t count, loff_t *data)
{
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
    if(command == 'R') {
        	sscanf(buf,"%c, %u, %u, %lu",&command,&pid,&period,&cpu_time);
			registration(pid,period,cpu_time);
            }
    else if(command == 'Y') {
            sscanf(buf,"%c, %u",&command,&pid);
			yielding(pid);
            }
    else if(command == 'D') {
    		sscanf(buf,"%c, %u",&command,&pid);
    		de_registration(pid);
        }
    else {
           	printk("invalid command\n");

    }

    kfree(buf);

    return count;

}

static const struct file_operations mp2_fops = {
    .owner   = THIS_MODULE,
    .read    = mp2_read,
    .write   = mp2_write
};

// mp2_init - Called when module is loaded
static int __init mp2_init(void)
{
    #ifdef DEBUG
    printk(KERN_ALERT "MP2 MODULE LOADING\n");
    #endif

	reg_dir = proc_mkdir("mp2",NULL);

	if(!reg_dir)
	{
		printk(KERN_INFO "directory creation failed\n");
		return -ENOMEM;
	}
	printk(KERN_INFO "proc dir successfully created\n");

	status_entry = proc_create("status",0666,reg_dir,&mp2_fops);
	if(!status_entry)
	{
		remove_proc_entry("mp2",NULL);
		printk(KERN_INFO "proc entry creation failed\n");
		return -ENOMEM;
	}

    // init spinlock
    spin_lock_init(&lock);

    // create cache
    mp2_cache = kmem_cache_create("mp2_task_struct",sizeof(struct mp2_task_struct),ARCH_MIN_TASKALIGN,SLAB_PANIC,NULL);

    // init and run the dispatching thread
    dispatcher = kthread_run(dispatch_thread, NULL, "dispatcher");

    printk(KERN_ALERT "MP2 MODULE LOADED\n");
    return 0;
}

// mp2_exit - Called when module is unloaded
static void __exit mp2_exit(void)
{
	struct list_head* pos, *n;
	struct mp2_task_struct* tmp;

    #ifdef DEBUG
    printk(KERN_ALERT "MP2 MODULE UNLOADING\n");
    #endif

    remove_proc_entry("status", reg_dir);
    remove_proc_entry("mp2", NULL);
    int ret = kthread_stop(dispatcher);
	if (ret != -EINTR)
		printk("Counter thread has stopped\n");
    mutex_destroy(&task_mutex);
    printk(KERN_ALERT "Mutex destroyed\n");
	list_for_each_safe(pos,n,&task_list){
		tmp = list_entry(pos, struct mp2_task_struct,list);
		list_del(&tmp->list);
		del_timer(&tmp->wakeup_timer);
		kmem_cache_free(mp2_cache,pos);
	}
    printk(KERN_ALERT "list content destroyed\n");
    kmem_cache_destroy(mp2_cache);
	list_del(&task_list);
    printk(KERN_ALERT "list destroyed\n");
    printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp2_init);
module_exit(mp2_exit);
