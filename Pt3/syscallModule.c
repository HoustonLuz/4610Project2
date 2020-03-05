#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/linkage.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/delay.h>

MODULE_LICENSE("Dual BSD/GPL");

#define BUF_LEN 2000
#define OFFLINE 0
#define IDLE 1
#define LOADING 2
#define UP 3
#define DOWN 4

typedef struct ListEntries {
	struct list_head list;
	char symbol;	// |: human, x: cat, o: dog
	int startFloor;
	int destFloor;
} ListEntries;

struct list_head queue[10];	// list of passengers on each floor
struct list_head elev;		// list of passengers on elevator
struct mutex queueMutex;
struct mutex elevMutex;
struct task_struct* run_thread;
static struct proc_dir_entry* proc_entry;
char* msg;
char* msg2;
static int procfs_buf_len;
static int waitFloor[10];	// num of passengers waiting at each floor
int state;
int nextState;
int animals;	// 0: none, 1: cat, 2: dog
int currentFloor;
int nextFloor;
int passengers;
int weight;		// current weight of elev, max load is 15
int waiting;	// total number waiting
int serviced;	// total number serviced
int stopping;	// set to 1 if elev stopping
static char str[10];
char onFloor;

extern long (*STUB_start_elev)(void);
long start_elev(void) {
	printk(KERN_NOTICE "Start elevator Test\n");
	if (state == OFFLINE) {
		state = IDLE;
		stopping = 0;
		currentFloor = 1;
		nextFloor = 2;
		passengers = 0;
		weight = 0;
		waiting = 0;
		animals = 0;
		serviced = 0;
		return 0;
	}
	else	// elevator already active
		return 1;
}

extern long (*STUB_issue_elev)(int,int,int,int);
long issue_elev(int num_pets, int pet_type, int start_floor, int destination_floor) {
	printk(KERN_NOTICE "Issue Request Test\n");
	if (start_floor == destination_floor) {
		serviced++;
		return 0;
	}
	struct ListEntries* qEntry;
	qEntry = kmalloc(sizeof(ListEntries), __GFP_RECLAIM);
	qEntry->symbol = '|';
	qEntry->startFloor = start_floor;
	qEntry->destFloor = destination_floor;
	mutex_lock_interruptible(&queueMutex);
	list_add_tail(&qEntry->list, &queue[start_floor - 1]);
	mutex_unlock(&queueMutex);
	if (num_pets > 0) {
		int i = 0;
		while (i < num_pets) {
			struct ListEntries* pEntry;
			pEntry = kmalloc(sizeof(ListEntries), __GFP_RECLAIM);
			if (pet_type == 1)
				pEntry->symbol = 'x';
			if (pet_type == 2)
				pEntry->symbol = 'o';
			pEntry->startFloor = start_floor;
			pEntry->destFloor = destination_floor;
			mutex_lock_interruptible(&queueMutex);
			list_add_tail(&pEntry->list, &queue[start_floor - 1]);
			mutex_unlock(&queueMutex);
			i++;
		}
	}
	waitFloor[start_floor - 1] += (1 + num_pets);
	waiting += (1 + num_pets);
	return 0;
}

extern long (*STUB_close_elev)(void);
long close_elev(void) {
	printk(KERN_NOTICE "Stop elevator Test\n");
	// offload all current passengers
	if (stopping == 1)
		return 1;
	stopping = 1;
	state = OFFLINE;
	return 0;
}

int unload(int floor) {	// returns 1 if entry unloaded, 0 if elev empty or couldn't unload
	printk(KERN_NOTICE "Unload\n");
	struct ListEntries *entry;
	struct list_head *temp, *end;
	int ifUnload = 0;

	//Check if any of the passengers need to get off
	mutex_lock_interruptible(&elevMutex);
	list_for_each_safe(temp, end, &elev) {
		entry = list_entry(temp, ListEntries, list);
		if(entry->destFloor == currentFloor){
			//Someone needs to get off
			if(entry->symbol == '|')
				weight -= 3;
			else if(entry->symbol == 'x')
				weight -= 2;
			else if(entry->symbol == 'o')
				weight -= 1;
			list_del(temp);
			kfree(entry);
			serviced++;
			ifUnload = 1;
		}
	}
	mutex_unlock(&elevMutex);

	animals = 0;

	mutex_lock_interruptible(&elevMutex);
	list_for_each_safe(temp, end, &elev) {
		entry = list_entry(temp, ListEntries, list);
		if(entry->symbol == 'x'){
			//Cats on board
			animals = 1;
		} else if(entry->symbol == 'o'){
			//Dogs on board
			animals = 2;
		}
	}
	mutex_unlock(&elevMutex);

	if(ifUnload == 0){
		//No one needs to get off
		return 0;
	} else {
		//Someone got off.
		return 1;
	}
}

int load(int floor) {	// returns 1 if entry loaded, 0 if elev full
	printk(KERN_NOTICE "load\n");
	int sum = 0,		//Sum of new party weight
	    animFlag = 0,	//0: none, 1: cat, 2: dog
		retFlag = 0,	// flag for return
		flag = 0,		// set to 1 if read first human
	    loadFlag = 0;		// if loading passengers this loop
	struct ListEntries* entry;
	struct list_head *temp, *end;

	if(weight >= 13){
		//Can not even fit next human. Elev is full.
		return 0;
	}

	mutex_lock_interruptible(&queueMutex);
	do {
		printk(KERN_WARNING "do loop\n");
		loadFlag = 0;
		flag = 0;
		list_for_each_safe(temp, end, &queue[floor - 1]) {
			entry = list_entry(temp, ListEntries, list);
			if ((entry->symbol == '\0') || !(entry->destFloor > currentFloor && nextState == UP) || (entry->destFloor < currentFloor && nextState == DOWN)) {
				// either floor is empty, or next passenger wants to go other direction
				break;
			}
			if (entry->symbol == '|' && flag == 0)
				flag = 1;
			if (flag == 1) {
				if (entry->symbol != '|') {
					if (entry->symbol == 'o') {
						sum += 2;
						animFlag = 2;
					} else {
						sum += 1;
						animFlag = 1;
					}
				}
			}
			if (animFlag != animals && animals != 0) {	// mismatch animal type, skip floor
				break;
			}
			if (sum + weight > 15) {
				break;
			}
			else
				loadFlag = 1;
		}	// end list for safe
		if (flag == 0)
			break;
		if (loadFlag == 1) {
			flag = 0;
			printk(KERN_WARNING "load to elevator\n");
			list_for_each_safe(temp, end, &queue[floor - 1]) {
				entry = list_entry(temp, ListEntries, list);
				// move passengers into elevator until next human
				if (entry->symbol == '|')
					flag = !flag;
				if (flag) {
					struct ListEntries* insert;
					insert = kmalloc(sizeof(ListEntries), __GFP_RECLAIM);
					insert->symbol = entry->symbol;
					if (insert->symbol == '|')
						weight += 3;
					else if (insert->symbol == 'o')
						weight += 2;
					else
						weight += 1;
 					insert->startFloor = entry->startFloor;
            		insert->destFloor = entry->destFloor;
            		mutex_lock_interruptible(&elevMutex);
      	    		list_add_tail(&insert->list, &elev);
            		list_del(temp);
            		kfree(entry);
            		mutex_unlock(&elevMutex);
				}
			}	// end list for safe
			retFlag = 1;
		}
	} while (weight < 13);
	mutex_unlock(&queueMutex);
	printk(KERN_NOTICE "Left do loop\n");
	if (retFlag == 1)
		return 1;
	else
		return 0;
}

int runElevator(void* data) {
	while (!kthread_should_stop()) {
		switch(state) {
			case OFFLINE:
				break;
			case IDLE:
				nextState = UP;
				if (load(currentFloor) && !stopping)
					state = LOADING;
				else {
					state = UP;
					nextFloor = currentFloor + 1;
				}
				break;
			case LOADING:
				ssleep(1);
				state = nextState;
				break;
			case UP:
			    ssleep(2);
			    currentFloor = nextFloor;
				if (unload(currentFloor) || load(currentFloor))
					state = LOADING;
				if (currentFloor == 10) {
					state = DOWN;
					nextState = DOWN;
					nextFloor = currentFloor - 1;
				}
				else
					nextFloor = currentFloor + 1;
				break;
			case DOWN:
				ssleep(2);
				currentFloor = nextFloor;
				if (unload(currentFloor) || load(currentFloor))
					state = LOADING;
				if (currentFloor == 1) {
					state = UP;
					nextState = UP;
					nextFloor = currentFloor + 1;
				}
				else
					nextFloor = currentFloor - 1;
				break;
		}
	}
	return 0;
}

static int procfile_open(struct inode* inode, struct file* file) {
	printk(KERN_INFO "proc_open\n");
	msg = kmalloc(sizeof(char) * 2048, __GFP_RECLAIM);
	msg2 = kmalloc(sizeof(char) * 2048, __GFP_RECLAIM);

	// change direction to string
	if (state == OFFLINE)
		sprintf(str, "OFFLINE");
	else if (state == IDLE)
		sprintf(str, "IDLE");
	else if (state == LOADING)
		sprintf(str, "LOADING");
	else if (state == UP)
		sprintf(str, "UP");
	else if (state == DOWN)
		sprintf(str, "DOWN");
	else
		sprintf(str, "ERROR");
	sprintf(msg, "Elevator state: %s\n", str);

	if (animals == 1)
		sprintf(str, "cat");
	else if (animals == 2)
		sprintf(str, "dog");
	else
		sprintf(str, "none");
	sprintf(msg2, "Elevator animals: %s\n", str);

	strcat(msg, msg2);
	sprintf(msg2, "Current floor: %d\n", currentFloor);
	strcat(msg, msg2);
	sprintf(msg2, "Number of passengers: %d\n", passengers);
	strcat(msg, msg2);
	sprintf(msg2, "Current weight: %d\n", weight);
	strcat(msg, msg2);
	sprintf(msg2, "Number of passengers waiting: %d\n", waiting);
	strcat(msg, msg2);
	sprintf(msg2, "Number of passengers serviced: %d\n\n\n", serviced);
	strcat(msg, msg2);


	// print queue interface
	int i = 10;
	char sym[2];
	sym[1] = ' ';
	struct list_head *temp;
	struct ListEntries* entry;
	mutex_lock_interruptible(&queueMutex);
	while (i > 0) {
		if (i == currentFloor)
			onFloor = '*';
		else
			onFloor = ' ';
		sprintf(msg2, "[%c] Floor %*d: %*d   ", onFloor, 2, i, 4, waitFloor[i - 1]);
		// print out symbols of passenger queue

		list_for_each(temp, &queue[i - 1]) {
			entry = list_entry(temp, ListEntries, list);
			sym[0] = entry->symbol;
			strcat(msg2, sym);
		}

		strcat(msg2, "\n");
		strcat(msg, msg2);
		i--;
	}
	mutex_unlock(&queueMutex);

	return 0;
}

static ssize_t procfile_read(struct file* file, char* ubuf, size_t count, loff_t* ppos) {
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

static int procfile_release(struct inode* inode, struct file* file) {
	printk(KERN_INFO "proc_release\n");
	kfree(msg);
	kfree(msg2);
	return 0;
}

static struct file_operations procfile_fops = {
	.owner = THIS_MODULE,
	.read = procfile_read,
	.open = procfile_open,
	.release = procfile_release,
};

static int elevator_init(void) {
	proc_entry = proc_create("elevator", 0666, NULL, &procfile_fops);
	if (proc_entry == NULL)
		return -ENOMEM;
	STUB_start_elev = &(start_elev);
	STUB_issue_elev = &(issue_elev);
	STUB_close_elev = &(close_elev);

	int i = 0;
	while (i < 10) {
		INIT_LIST_HEAD(&queue[i]);
		i++;
	}
	INIT_LIST_HEAD(&elev);
	mutex_init(&queueMutex);
	mutex_init(&elevMutex);
	run_thread = kthread_run(runElevator, NULL, "Elevator Thread");
	return 0;
}

static void elevator_exit(void) {
	proc_remove(proc_entry);
	STUB_start_elev = NULL;
	STUB_issue_elev = NULL;
	STUB_close_elev = NULL;
	mutex_destroy(&queueMutex);
	mutex_destroy(&elevMutex);
	kthread_stop(run_thread);
	return;
}

module_init(elevator_init);
module_exit(elevator_exit);

