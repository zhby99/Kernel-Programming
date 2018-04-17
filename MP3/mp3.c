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

#define DIRECTORY   "mp3"
#define FILE        "status"
#define PAGE_NUM    128
#define DELAY_TIME  50

void registration(unsigned int);
void unregistration(unsigned int);

typedef struct {
	struct list_head list;
	struct task_struct* task;
	unsigned int pid;
}mp3_task_struct;


struct proc_dir_entry *proc_directory, *proc_file;

static struct workqueue_struct *my_wq;
static spinlock_t lock;
static unsigned int cur_len = 0;
static unsigned long * buffer;
static struct hrtimer htimer;
static ktime_t kt_periode;
static struct cdev mp3_cdev;
static dev_t mp3_devt;
LIST_HEAD(task_list);

static ssize_t mp3_read (struct file *file, char __user *buffer, size_t count, loff_t *data){
	ssize_t copied = 0;
	char * buf = (char *) kmalloc(count,GFP_KERNEL);
	mp3_task_struct* tmp;
	unsigned long flags;
	spin_lock_irqsave(&lock,flags);
	list_for_each_entry(tmp, &task_list, list){
		copied+=sprintf(buf+copied,"PID: %u\n",tmp->pid);
	}
	spin_unlock_irqrestore(&lock,flags);
	buf[copied] = '\0';
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

static ssize_t mp3_write (struct file *file, const char __user *buffer, size_t count, loff_t *data){
	char* buf;
	char command;
	unsigned int pid;
	buf = (char *) kmalloc(count,GFP_KERNEL);
	if(copy_from_user(buf,buffer, count)){
		kfree(buf);
		return -EFAULT;
	}
	buf[count] = '\0';
	command = buf[0];
    switch (command) {
        case 'R':
            sscanf(buf,"%c %u",&command,&pid);
            registration(pid);
            break;
        case 'U':
            sscanf(buf,"%c %u",&command,&pid);
            unregistration(pid);
            break;
        default:
            printk("invalid command!\n");
    }
	kfree(buf);
	return count;
}

static const struct file_operations mp3_fops = {
	.write= mp3_write,
	.read= mp3_read,
};

// helper function!
mp3_task_struct *get_task_by_pid(int pid){
    mp3_task_struct *tmp;
    list_for_each_entry(tmp, &task_list, list) {
        if (tmp->pid == pid) {
            return tmp;
        }
    }
    return NULL;
}

void registration(unsigned int pid){
	unsigned long flags;
	mp3_task_struct* tmp = kmalloc(sizeof(mp3_task_struct),GFP_KERNEL);
	tmp->pid = pid;
	tmp->task = find_task_by_pid(pid);
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
	mp3_task_struct *tmp;
	unsigned long flags;
	spin_lock_irqsave(&lock,flags);
    tmp = get_task_by_pid(pid);
    if(!tmp) {
        spin_unlock_irqrestore(&lock,flags);
        return;
    }
    list_del(&tmp->list);
    kfree(tmp);
    if (list_empty(&task_list)) {
        hrtimer_cancel(& htimer);
        flush_workqueue(my_wq);
        destroy_workqueue(my_wq);
    }
    spin_unlock_irqrestore(&lock,flags);
    printk("task %u unregistered!\n",pid);
	return;
}

void my_wq_function(struct work_struct *work) {
	mp3_task_struct *tmp;
	unsigned long flags, min_flt, maj_flt, utime, stime;
	unsigned long all_min_flt=0, all_maj_flt=0, all_time=0;

	spin_lock_irqsave(&lock, flags);
	list_for_each_entry(tmp, &task_list, list) {
		if(get_cpu_use(tmp->pid, &min_flt, &maj_flt, &utime, &stime) == 0) {
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
	buffer[cur_len] = -1;
	if(cur_len >= PAGE_NUM * PAGE_SIZE / sizeof(unsigned long)){
		cur_len = 0;
		printk("memory buffer is full and it starts over!\n");
	}
	kfree(work);
}


static enum hrtimer_restart timer_function(struct hrtimer * timer){
	unsigned long flags;
	struct work_struct* my_work = (struct work_struct*)kmalloc(sizeof(struct work_struct),GFP_KERNEL);
	INIT_WORK(my_work, my_wq_function);
	spin_lock_irqsave(&lock,flags);
	queue_work(my_wq, my_work);
	spin_unlock_irqrestore(&lock,flags);
	hrtimer_forward_now(timer, kt_periode);
	return HRTIMER_RESTART;
}

static void timer_init(void){
	kt_periode = ktime_set(0, DELAY_TIME*1E6L); //seconds,nanoseconds
	hrtimer_init (& htimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
	htimer.function = timer_function;
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
    .open    = NULL,
    .release = NULL,
    .mmap    = cdev_mmap,
};

static int __init mp3_init(void)
{
	#ifdef DEBUG
	printk(KERN_ALERT "MP3 MODULE LOADING\n");
	#endif

    proc_directory = proc_mkdir(DIRECTORY, NULL);
    if (!proc_directory) {
        remove_proc_entry(DIRECTORY, NULL);
        printk(KERN_ALERT "Error: Could not initialize /proc/%s\n", DIRECTORY);
        return -ENOMEM;
    }
    // create entry for FILE
    proc_file = proc_create(FILE, 0666, proc_directory, &mp3_fops);
    if (!proc_file) {
        remove_proc_entry(FILE, proc_directory);
        remove_proc_entry(DIRECTORY, NULL);
        printk(KERN_ALERT "Error: Could not initialize /proc/%s/%s\n", DIRECTORY, FILE);
        return -ENOMEM;
    }


	buffer = (unsigned long*)vmalloc(PAGE_NUM * PAGE_SIZE);
	if(!buffer){
		remove_proc_entry(FILE,proc_directory);
		remove_proc_entry(DIRECTORY,NULL);
		return -ENOMEM;
	}
    memset(buffer, 0, PAGE_NUM * PAGE_SIZE);
	printk(KERN_INFO "virtual memory allocated\n");

	if(alloc_chrdev_region(&mp3_devt, 0, 1, "mp3_cdev") < 0){
        remove_proc_entry(FILE,proc_directory);
		remove_proc_entry(DIRECTORY,NULL);
		vfree(buffer);
		return -ENOMEM;
	}
	printk(KERN_INFO "character device %u registered\n",mp3_devt);

	cdev_init(&mp3_cdev, &mp3_cdev_op);
	if(cdev_add(&mp3_cdev,mp3_devt,1)){
        remove_proc_entry(FILE,proc_directory);
		remove_proc_entry(DIRECTORY,NULL);
		cdev_del(&mp3_cdev);
		vfree(buffer);
		return -ENOMEM;
	}
	timer_init();
	spin_lock_init(&lock);
	printk(KERN_ALERT "MP3 MODULE LOADED\n");
	return 0;
}

// mp3_exit - Called when module is unloaded
static void __exit mp3_exit(void){
	struct list_head* pos, *n;
	mp3_task_struct *entry, *temp_entry;

	#ifdef DEBUG
	printk(KERN_ALERT "MP3 MODULE UNLOADING\n");
	#endif
    remove_proc_entry(FILE,proc_directory);
    remove_proc_entry(DIRECTORY,NULL);

    list_for_each_entry_safe(entry, temp_entry, &task_list, list){
        list_del(&(entry->list));
        kfree(entry);
    }
	list_del(&task_list);
	cdev_del(&mp3_cdev);
	unregister_chrdev_region(mp3_devt, 1);
	if(buffer)
		vfree(buffer);

	printk(KERN_ALERT "MP3 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp3_init);
module_exit(mp3_exit);
