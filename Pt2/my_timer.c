// my_timer module to store the current and elapsed time since the last call
// COP4610 Project 2: Part 2
// Group members: Brandon Matulonis, Houston Luzader

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/time.h>

MODULE_LICENSE("Dual BSD/GPL");

#define BUF_LEN 100

static struct proc_dir_entry* proc_entry;

static char msg[BUF_LEN];
static char msg2[BUF_LEN];
static int procfs_buf_len;
static int numCalls = 0;	// number of proc calls
struct timespec currentTime;	// stores current time in .tv_sec and .tv_nsec
long seconds;			// seconds and ns since last call
long nanoseconds;

static int procfile_open(struct inode* inode, struct file* file)
{
	printk(KERN_INFO "proc_open\n");
	numCalls++;

	currentTime = current_kernel_time();	// saves format seconds.nanoseconds
	sprintf(msg, "current time: %ld.%ld\n", currentTime.tv_sec, currentTime.tv_nsec);

	// get elapsed time since last call
	seconds = currentTime.tv_sec - seconds;

	// shift seconds to nanoseconds
	if (currentTime.tv_nsec - nanoseconds < 0)
	{
		seconds--;
		nanoseconds = currentTime.tv_nsec - nanoseconds + 1000000000;
	}
	else
		nanoseconds = currentTime.tv_nsec - nanoseconds;

	// if not first call, show elapsed time as well in the msg
	if (numCalls > 1)
	{
		sprintf(msg2, "elapsed time: %ld.%ld\n", seconds, nanoseconds);
		strcat(msg, msg2);
	}

	seconds = currentTime.tv_sec;
	nanoseconds = currentTime.tv_nsec;

	return 0;
}

static ssize_t procfile_read(struct file* file, char * ubuf, size_t count, loff_t *ppos)
{
	printk(KERN_INFO "proc_read\n");
	procfs_buf_len = strlen(msg);

	if (*ppos > 0 || count < procfs_buf_len)
		return 0;

	if (copy_to_user(ubuf, msg, procfs_buf_len))
		return -EFAULT;

	*ppos = procfs_buf_len;

	printk(KERN_INFO "gave to user %s\n", msg);

	return procfs_buf_len;
}

static struct file_operations procfile_fops = {
	.owner = THIS_MODULE,
	.read = procfile_read,
	.open = procfile_open,
};

static int timer_init(void)
{
	proc_entry = proc_create("timed", 0666, NULL, &procfile_fops);

	if (proc_entry == NULL)
		return -ENOMEM;

	return 0;
}

static void timer_exit(void)
{
	proc_remove(proc_entry);
	return;
}



module_init(timer_init);
module_exit(timer_exit);
