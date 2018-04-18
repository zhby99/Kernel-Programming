#define LINUX

#include "mp3_given.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <asm/uaccess.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("23");
MODULE_DESCRIPTION("CS-423 MP3");


typedef struct mp3_task_struct{
	struct list_head list;
	struct task_struct* linux_task;
	unsigned int pid;

}mp3_task_struct;

static struct proc_dir_entry *reg_dir;
static struct proc_dir_entry *status_entry;
static struct workqueue_struct *my_wq;
static spinlock_t lock;
static unsigned int cur_len = 0;
static unsigned int BUFFER_LEN = 128*PAGE_SIZE; //(NPAGES * PAGE_SIZE / sizeof(unsigned long))
static unsigned long * buffer;
static unsigned long delay_time = 50;
static struct hrtimer htimer;
static ktime_t kt_periode;
static struct cdev mp3_cdev;
static dev_t mp3_devt;
LIST_HEAD(task_list);

void my_wq_function(struct work_struct *work) {

	//printk("work queue job awake\n");
	struct mp3_task_struct *pos;
	unsigned long flags, min_flt, maj_flt, utime, stime;
	unsigned long all_min_flt=0, all_maj_flt=0, all_time=0;

	spin_lock_irqsave(&lock, flags);
	list_for_each_entry(pos, &task_list, list) {
		if(get_cpu_use(pos->pid, &min_flt, &maj_flt, &utime, &stime) == 0) {
			all_maj_flt = all_maj_flt + maj_flt;
			all_min_flt = all_min_flt + min_flt;
			all_time = all_time + utime + stime;
		}
	}
	spin_unlock_irqrestore(&lock, flags);
	buffer[cur_len++] = jiffies;
	buffer[cur_len++] = all_min_flt;
	buffer[cur_len++] = all_maj_flt;
	buffer[cur_len++] = jiffies_to_msecs(cputime_to_jiffies(all_time));

	//marking the end
	buffer[cur_len] = -1;

	if(cur_len + 4 >= BUFFER_LEN){
		cur_len = 0;
		printk("buffer start over\n");
	}

	kfree(work);
	//printk("work queue job sleep\n");
}


static enum hrtimer_restart timer_function(struct hrtimer * timer)
{
	// @Do your work here.
	unsigned long flags;
	struct work_struct* my_work = (struct work_struct*)kmalloc(sizeof(struct work_struct),GFP_KERNEL);
	INIT_WORK(my_work, my_wq_function);

	spin_lock_irqsave(&lock,flags);
	queue_work(my_wq, my_work);
	//printk("work in queue\n");
	spin_unlock_irqrestore(&lock,flags);

	hrtimer_forward_now(timer, kt_periode);

	//printk("my_timer_callback called\n");
	return HRTIMER_RESTART;
}

static void timer_init(void)
{
	kt_periode = ktime_set(0, delay_time*1E6L); //seconds,nanoseconds
	hrtimer_init (& htimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
	htimer.function = timer_function;
}

static void timer_cleanup(void)
{
	hrtimer_cancel(& htimer);
}


void registration(unsigned int pid){
	printk("task %u arrived\n",pid);
	unsigned long flags;
	struct mp3_task_struct* tmp = kmalloc(sizeof(mp3_task_struct),GFP_KERNEL);
	tmp->pid = pid;
	tmp->linux_task = find_task_by_pid(pid);

	INIT_LIST_HEAD(&tmp->list);
	spin_lock_irqsave(&lock,flags);

	if(list_empty(&task_list)){
		my_wq = create_workqueue("my_queue");
		hrtimer_start(& htimer, kt_periode, HRTIMER_MODE_REL);
		printk("work queue job created\n");
	}

	list_add_tail(&tmp->list, &task_list);
	spin_unlock_irqrestore(&lock,flags);
	printk("task %u registered!\n",pid);
}

void unregistration(unsigned int pid){

	printk("task %u is leaving\n",pid);
	struct list_head *pos, *n;
	struct mp3_task_struct *tmp;
	unsigned long flags;
	spin_lock_irqsave(&lock,flags);
	list_for_each_safe(pos,n,&task_list){
		tmp = list_entry(pos,struct mp3_task_struct,list);
		if(tmp->pid == pid){
			printk("we find you!\n");
			list_del(&tmp->list);
			kfree(tmp);
			if (list_empty(&task_list)) {
				timer_cleanup();
				flush_workqueue(my_wq);
				destroy_workqueue(my_wq);
			}

			spin_unlock_irqrestore(&lock,flags);
			printk("task %u de-registered!\n",pid);
			return;
		}
	}

	spin_unlock_irqrestore(&lock,flags);
	printk("invalid de-registration");
	return;
}

static ssize_t mp3_write (struct file *file, const char __user *buffer, size_t count, loff_t
*data){

	char* buf;
	char command;
	unsigned int pid;

	buf = (char *) kmalloc(count,GFP_KERNEL);
	if(copy_from_user(buf,buffer, count)){
		kfree(buf);
		return -EFAULT;
	}
	buf[count] = 0;
	command = buf[0];

	if(command == 'R'){
		sscanf(buf,"%c %u",&command,&pid);
		registration(pid);
	}
	else if(command == 'U'){
		sscanf(buf,"%c %u",&command,&pid);
		unregistration(pid);
	}
	else{
		printk("invalid command %c\n",command);
	}

	//printk("command %s\n",buf);
	kfree(buf);

	return count;
}

static ssize_t mp3_read (struct file *file, char __user *buffer, size_t count, loff_t *data){

	ssize_t copied = 0;
	char * buf = (char *) kmalloc(count,GFP_KERNEL);
	struct list_head* pos;
	struct mp3_task_struct* tmp;
	unsigned long flags;

	spin_lock_irqsave(&lock,flags);
	list_for_each(pos,&task_list){
		tmp = list_entry(pos,struct mp3_task_struct,list);
		copied+=sprintf(buf+copied,"PID: %u\n",tmp->pid);
	}
	spin_unlock_irqrestore(&lock,flags);

	buf[copied] = 0;
	if(*data >= copied){
		kfree(buf);
		return 0;
	}

	if(copy_to_user(buffer, buf, copied)){
		kfree(buf);
		printk("Error in read\n");
		return -EINVAL;
	}
	*data+=copied;

	kfree(buf);

	return copied;
}

static const struct file_operations mp3_fops = {
	.owner = THIS_MODULE,
	.write= mp3_write,
	.read= mp3_read,
};

static int cdev_open(struct inode *inode, struct file *file){
	printk("cdev opened\n");
	return 0;
}

static int cdev_close(struct inode *inode, struct file *file){
	printk("cdev closed\n");
	return 0;
}


static int cdev_mmap(struct file *file, struct vm_area_struct *vm) {

	unsigned long left_size = vm->vm_end - vm->vm_start;
	void *buffer_pointer = buffer;
	unsigned long start = vm->vm_start;

	while(left_size > 0) {
		unsigned long pfn = vmalloc_to_pfn(buffer_pointer);
		unsigned long copy_size = (left_size < PAGE_SIZE)? left_size : PAGE_SIZE;
		int flag = remap_pfn_range(vm, start, pfn, copy_size, PAGE_SHARED);
		if(flag < 0)
			return flag;

		start+=PAGE_SIZE;
		buffer_pointer+=PAGE_SIZE;
		left_size-=PAGE_SIZE;
	}
	printk("dev op finished\n");
	return 0;
}

static const struct file_operations mp3_cdev_op = {
    .owner   = THIS_MODULE,
    .open    = cdev_open,
    .release = cdev_close, // close
    .mmap    = cdev_mmap,
};

static int __init mp3_init(void)
{
	#ifdef DEBUG
	printk(KERN_ALERT "MP3 MODULE LOADING\n");
	#endif

	reg_dir = proc_mkdir("mp3",NULL);
	if(!reg_dir)
	{
		printk(KERN_INFO "directory creation failed\n");
		return -ENOMEM;
	}
	printk(KERN_INFO "proc dir created\n");

	status_entry = proc_create("status",0666,reg_dir,&mp3_fops);
	if(!status_entry)
	{
		remove_proc_entry("mp3",NULL);
		printk(KERN_INFO "proc entry creation failed\n");
		return -ENOMEM;
	}
	printk(KERN_INFO "file created\n");

	buffer = vmalloc(BUFFER_LEN*sizeof(unsigned long));
	if(!buffer){
		remove_proc_entry("status",reg_dir);
		remove_proc_entry("mp3",NULL);
		return -ENOMEM;
	}
	printk(KERN_INFO "virtual memory allocated\n");

	if(alloc_chrdev_region(&mp3_devt, 0, 1, "mp3_cdev") < 0){
		remove_proc_entry("status",reg_dir);
		remove_proc_entry("mp3",NULL);
		vfree(buffer);
		return -ENOMEM;
	}
	printk(KERN_INFO "character device %u registered\n",mp3_devt);

	cdev_init(&mp3_cdev, &mp3_cdev_op);
	if(cdev_add(&mp3_cdev,mp3_devt,1)){
		remove_proc_entry("status",reg_dir);
		remove_proc_entry("mp3",NULL);
		cdev_del(&mp3_cdev);
		vfree(buffer);
		return -ENOMEM;
	}
	printk(KERN_INFO "character device added\n");

	timer_init();
	printk(KERN_INFO "timer initialized\n");

	/*my_wq = create_workqueue("my_queue");
	if(!my_wq)
		printk(KERN_INFO "workqueue creation failed\n");
	else
		printk(KERN_INFO "workqueue initialized\n");
	*/

	//initialize spinlock
	spin_lock_init(&lock);
	printk(KERN_INFO "lock initialized\n");

	printk(KERN_ALERT "MP3 MODULE LOADED\n");
	return 0;
}

// mp3_exit - Called when module is unloaded
static void __exit mp3_exit(void)
{

	struct list_head* pos, *n;
	struct mp3_task_struct* tmp;

	#ifdef DEBUG
	printk(KERN_ALERT "MP3 MODULE UNLOADING\n");
	#endif
	// Insert your code here ...

	remove_proc_entry("status",reg_dir);
	remove_proc_entry("mp3",NULL);

	list_for_each_safe(pos,n,&task_list){
		tmp = list_entry(pos,mp3_task_struct,list);
		//printk(KERN_ALERT "Remove element %d: time %d\n",tmp->pid,tmp->cpu_time);
		list_del(&tmp->list);
		kfree(tmp);
	}

	list_del(&task_list);
	printk(KERN_INFO "list destroyed\n");

/*
	timer_cleanup();
	printk(KERN_INFO "timer destroyed\n");


	flush_workqueue(my_wq);
	destroy_workqueue(my_wq);
	printk(KERN_INFO "workqueue destroyed\n");
*/

	cdev_del(&mp3_cdev);
	printk(KERN_INFO "character device deleted\n");

	unregister_chrdev_region(mp3_devt, 1);
	printk(KERN_INFO "character device unregistered\n");

	if(buffer)
		vfree(buffer);
	printk(KERN_INFO "virtual memory freed\n");

	printk(KERN_ALERT "MP3 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp3_init);
module_exit(mp3_exit);
