#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/syscalls.h>

/* System call stub */
long (*STUB_start_elev)(void) = NULL;
EXPORT_SYMBOL(STUB_start_elev);
long (*STUB_issue_elev)(int,int,int,int) = NULL;
EXPORT_SYMBOL(STUB_issue_elev);
long (*STUB_close_elev)(void) = NULL;
EXPORT_SYMBOL(STUB_close_elev);

/* System call wrapper */
SYSCALL_DEFINE0(start_elev) {
	if (STUB_start_elev != NULL)
		return STUB_start_elev();
	else
		return -ENOSYS;
}

SYSCALL_DEFINE4(issue_elev, int, num_pets, int, pet_type, int, start_floor, int, destination_floor) {
	if (STUB_issue_elev != NULL)
		return STUB_issue_elev(num_pets, pet_type, start_floor, destination_floor);
	else
		return -ENOSYS;
}

SYSCALL_DEFINE0(close_elev) {
	if (STUB_close_elev != NULL)
		return STUB_close_elev();
	else
		return -ENOSYS;
}
