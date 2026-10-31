/*
 * Copyright (C) 2021 Unisoc Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "unisoc_userlog: " fmt

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/time64.h>
#include <linux/vmalloc.h>
#include <linux/uio.h>
#include <linux/sched/signal.h>
#include "unisoc_userlog.h"
#include <linux/signal.h>
#include <linux/sched/task.h>
#include <trace/hooks/signal.h>
#include <trace/hooks/binder.h>

#include <trace/hooks/sched.h>

#include <linux/cgroup.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/freezer.h>
#include <linux/sched/jobctl.h>


#include <../../android/binder_internal.h>
#include <../../android/binder_alloc.h>
#ifndef ZTE_FEATURE_CGROUP_FREEZER_V2
#define ZTE_FEATURE_CGROUP_FREEZER_V2            true
#endif

#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

struct userlog_log {
	unsigned char		*buffer;
	struct miscdevice	logdev;
	wait_queue_head_t	wq;
	struct list_head	readers;
	struct mutex		mutex;
	size_t				w_off;
	size_t				head;
	size_t				size;
	size_t				free;
};

struct userlog_reader {
	struct userlog_log	*log;
	struct list_head	list;
	size_t				r_off;
};

struct userlog_log *glog;


#if true == ZTE_FEATURE_CGROUP_FREEZER_V2
#define UID_SIZE	100
#define STATE_SIZE	10
#define MIN_FREE_ASYNC_BINDER_BUFFER_SIZE (50 * 1024)
static struct device *dev = NULL;
static int state = 1;
static struct workqueue_struct *unfreeze_eventqueue = NULL;
static struct send_event_data
{
	char *type;
	unsigned int uid;
	unsigned int pid;
	struct work_struct sendevent_work;
} *wsdata;


enum freezer_state_flags {
	CGROUP_FREEZER_ONLINE	= (1 << 0), /* freezer is fully online */
	CGROUP_FREEZING_SELF	= (1 << 1), /* this freezer is freezing */
	CGROUP_FREEZING_PARENT	= (1 << 2), /* the parent freezer is freezing */
	CGROUP_FROZEN		= (1 << 3), /* this and its descendants frozen */

	/* mask for all FREEZING flags */
	CGROUP_FREEZING		= CGROUP_FREEZING_SELF | CGROUP_FREEZING_PARENT,
};

struct freezer {
	struct cgroup_subsys_state	css;
	unsigned int			state;
};


static inline struct freezer *css_freezer(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct freezer, css) : NULL;
}

static inline struct freezer *task_freezer(struct task_struct *task)
{
	return css_freezer(task_css(task, freezer_cgrp_id));
}


bool cgroup_freezing_v1(struct task_struct *task)
{
	bool ret;

	rcu_read_lock();
	ret = task_freezer(task)->state & (CGROUP_FREEZING | CGROUP_FROZEN);
	rcu_read_unlock();

	return ret;
}

bool cgroup_is_cgroup2_freezing(struct task_struct *task)
{
	bool ret = false;
	struct cgroup *task_cgp = NULL;

	task_cgp = task_dfl_cgroup(task);
	if (task_cgp != NULL) {
		ret = task_cgp->freezer.freeze;
	}
	return ret;
}

static void sendevent_handler(struct work_struct *work)
{
	struct send_event_data *temp = container_of(work, struct send_event_data, sendevent_work);
	char buf[UID_SIZE] = {0};
	char *envp[2] = {buf, NULL};
	char *type = NULL;
	int uid = 0;
	int pid = 0;

	uid = temp->uid;
	type = temp->type;
	pid = temp->pid;

	if (pid != 0) {
		snprintf(buf, UID_SIZE, "%sUID=%u:%u", type, uid, pid);
	} else {
		snprintf(buf, UID_SIZE, "%sUID=%u", type, uid);
	}
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
	kfree(temp);
	temp = NULL;
	pr_info("have send event uid is %u, reason is %s\n", uid, type);
}
void send_unfreeze_event_withpid(char *type, unsigned int uid, unsigned int pid)
{
	pr_info("Need send event uid is %u, reason is %s\n", uid, type);
	if (state == 0) {
		pr_err("cgroup event module state is %d, not send uevent!!!\n", state);
		return;
	}
	wsdata = kzalloc(sizeof(struct send_event_data), GFP_ATOMIC);
	if (wsdata == NULL) {
		pr_err("send event malloc workqueue data is error!!!\n");
		return;
	}
	wsdata->type = type;
	wsdata->uid = uid;
	wsdata->pid = pid;
	INIT_WORK(&(wsdata->sendevent_work), sendevent_handler);
	queue_work(unfreeze_eventqueue, &(wsdata->sendevent_work));
}

void send_unfreeze_event(char *type, unsigned int uid)
{
	/* pr_info("Need send event uid is %u, reason is %s\n", uid, type); */

	if (state == 0) {
		pr_err("cgroup event module state is %d, not send uevent!!!\n", state);
		return;
	}

	wsdata = kzalloc(sizeof(struct send_event_data), GFP_ATOMIC);
	if (wsdata == NULL) {
		pr_err("send event malloc workqueue data is error!!!\n");
		return;
	}
	wsdata->type = type;
	wsdata->uid = uid;
	INIT_WORK(&(wsdata->sendevent_work), sendevent_handler);
	queue_work(unfreeze_eventqueue, &(wsdata->sendevent_work));
}

static void send_unfreeze_event_test(char *buf)
{
	char *s_c[2] = {buf, NULL};

	if (state == 0) {
		pr_err("cgroup event module state is %d, not send uevent!!!\n", state);
		return;
	}
	send_unfreeze_event("KILL", 1000);
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, s_c);
}



static ssize_t unfreeze_show_state(struct device *dev, struct device_attribute *attr, char *buf)
{
	pr_info("show unfreeze event state is %d\n", state);
	return snprintf(buf, STATE_SIZE, "%d\n", state);
}

static ssize_t unfreeze_set_state(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	size_t ret = -EINVAL;

	ret = kstrtoint(buf, STATE_SIZE, &state);
	if (ret < 0)
		return ret;

	pr_info("set unfreeze event state is %d\n", state);
	return count;
}

static ssize_t send(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	char unfreeze_uid[UID_SIZE] = {0};

	snprintf(unfreeze_uid, sizeof(unfreeze_uid), "UID=%s", buf);
	send_unfreeze_event_test(unfreeze_uid);
	pr_info("send unfreeze event %s\n", unfreeze_uid);
	return count;
}



static struct class unfreeze_event_class = {
	.name = "cpufreezer",
};
#endif


#if true == ZTE_FEATURE_CGROUP_FREEZER_V2

static DEVICE_ATTR(test, S_IRUGO|S_IWUSR, NULL, send);
static DEVICE_ATTR(state, S_IRUGO|S_IWUSR, unfreeze_show_state, unfreeze_set_state);


static const struct attribute *unfreeze_event_attr[] = {
	&dev_attr_test.attr,
	&dev_attr_state.attr,
	NULL,
};
void signal_catch_for_freeze(void *data, int sig, struct task_struct *killer, struct task_struct *dst)
{
	if (sig == SIGQUIT || sig == SIGABRT || sig == SIGKILL || sig == SIGSEGV) {
		if (cgroup_freezing_v1(dst)) {
			//send_unfreeze_event("KILL", (unsigned int)(dst->real_cred->uid.val));
			send_unfreeze_event_withpid("KILL", (unsigned int)(dst->real_cred->uid.val), (unsigned int)(dst->pid));
			pr_warn("send_unfreeze_event signal = %d  %d  %d\n", sig, dst->real_cred->uid.val, dst->pid);
		} else if (cgroup_is_cgroup2_freezing(dst)) {
			send_unfreeze_event("KILL", (unsigned int)(dst->real_cred->uid.val));
			pr_warn("send_unfreeze_event cgroup2 signal = %d  %d  %d\n", sig, dst->real_cred->uid.val, dst->pid);
		}
	}
}
static const struct attribute_group unfreeze_event_attr_group = {
	.attrs = (struct attribute **) unfreeze_event_attr,
};

void binder_catch_for_freeze_notify(void *data, struct binder_proc *proc, struct binder_transaction *t, struct task_struct *binder_th_task, bool pending_async, bool sync)
{
	struct task_struct *binder_proc_task = NULL;

	if (proc != NULL) {
		binder_proc_task = proc->tsk;
	}
	if (binder_proc_task != NULL && binder_proc_task->real_cred->uid.val > 10000 && sync && (cgroup_is_cgroup2_freezing(binder_proc_task) || cgroup_freezing_v1(binder_proc_task))) {
		send_unfreeze_event("BINDER", (unsigned int)(binder_proc_task->real_cred->uid.val));
	}

}

void binder_trans_for_freeze_notify(void *data, struct binder_proc *target_proc, struct binder_proc *proc, struct binder_thread *thread, struct binder_transaction_data *tr)
{
	if ((target_proc != NULL) && (target_proc->tsk != NULL) && (target_proc->tsk->real_cred->uid.val > 10000) && (tr != NULL) && (tr->flags & 0x01) && (cgroup_is_cgroup2_freezing(target_proc->tsk) || cgroup_freezing_v1(target_proc->tsk))) {
		struct binder_alloc *alloc = (struct binder_alloc *)&target_proc->alloc;

		if (alloc != NULL) {
			size_t free = alloc->free_async_space;
			//pr_warn("binder_trans_for_freeze_notify: target_uid=%d, free=%d, min=%d\n", target_proc->tsk->real_cred->uid.val, free, MIN_FREE_ASYNC_BINDER_BUFFER_SIZE);
			if (free <= MIN_FREE_ASYNC_BINDER_BUFFER_SIZE) {
				pr_warn("send_unfreeze_event uid=%d  insufficient free_async_mem=%zu\n", target_proc->tsk->real_cred->uid.val, free);
				send_unfreeze_event("BINDER", (unsigned int)target_proc->tsk->real_cred->uid.val);
			}
		}
	}
}
#endif



/* return userlog offset */

static size_t userlog_offset(struct userlog_log *log, size_t n)
{
	return n & (log->size - 1);
}

/* read sub function start */

static struct userlog_entry *get_entry_header(struct userlog_log *log,
						size_t off, struct userlog_entry *scratch)
{
	size_t len = min(sizeof(struct userlog_entry), log->size - off);

	if (len != sizeof(struct userlog_entry)) {
		memcpy(((void *)scratch), log->buffer + off, len);
		memcpy(((void *)scratch) + len, log->buffer,
		       sizeof(struct userlog_entry) - len);
		return scratch;
	}

	return (struct userlog_entry *) (log->buffer + off);
}

static __u32 get_entry_msg_len(struct userlog_log *log, size_t off)
{
	struct userlog_entry scratch;
	struct userlog_entry *entry;

	entry = get_entry_header(log, off, &scratch);
	return entry->len;
}

static ssize_t do_read_log_to_user(struct userlog_log *log,
					struct userlog_reader *reader,
					char __user *buf,
					size_t count)
{
	struct userlog_entry scratch;
	struct userlog_entry *entry;
	size_t len;
	size_t msg_start;

	/* don't copy the header to userspace */
	entry = get_entry_header(log, reader->r_off, &scratch);

	/* calculate count and buf */
	count -= sizeof(struct userlog_entry);
	msg_start = userlog_offset(log,
					reader->r_off + sizeof(struct userlog_entry));

	/* copy sysmsg to userspace */
	len = min(count, log->size - msg_start);
	if (copy_to_user(buf, log->buffer + msg_start, len))
		return -EFAULT;

	if (count != len) {
		if (copy_to_user(buf + len, log->buffer, count - len))
			return -EFAULT;
	}

	/* calculate next reader offset */
	reader->r_off = userlog_offset(log, reader->r_off +
		sizeof(struct userlog_entry) + count);

	return count;
}

/* read sub function end */

/* userlog_read - our log's read() method */

static ssize_t userlog_read(struct file *file, char __user *buf,
					size_t count, loff_t *pos)
{
	struct userlog_reader *reader = file->private_data;
	struct userlog_log *log = glog;
	ssize_t ret;
	ssize_t entry_size;
	DEFINE_WAIT(wait);

	/* read userlog check */
	while (1) {
		mutex_lock(&log->mutex);

		prepare_to_wait(&log->wq, &wait, TASK_INTERRUPTIBLE);

		ret = (glog->w_off == reader->r_off);
		mutex_unlock(&log->mutex);
		if (!ret)
			break;

		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		schedule();
	}
	finish_wait(&log->wq, &wait);
	if (ret)
		return ret;
	/* read userlog start */
	mutex_lock(&log->mutex);

	/* get the size of the next entry*/
	entry_size = sizeof(struct userlog_entry) + get_entry_msg_len(log, reader->r_off);

	/* get exactly one entry from the log */
	ret = do_read_log_to_user(log, reader, buf, entry_size);

	mutex_unlock(&log->mutex);

	return ret;
}

/* userlog_write_iter - implementing support for write()*/

static ssize_t userlog_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct userlog_log *log = glog;
	struct userlog_entry header;
	struct userlog_reader *reader;
	size_t len, count, w_off, oldhead;
	ssize_t entry_size;

	count = min_t(size_t, iov_iter_count(from), USERLOG_ENTRY_MAX_PAYLOAD);

	/* set header info */
	header.len = count;

	/* check write count zero, return 0 */
	if (unlikely(!header.len))
		return 0;

	mutex_lock(&log->mutex);

	/* rollover messages until the free space is enough */
	while (sizeof(header) + count + 1 > log->free) {
		/* calculate the entry size at the head location */
		entry_size = sizeof(struct userlog_entry) + get_entry_msg_len(log, log->head);
		oldhead = log->head;

		/* update the head offset */
		log->head = userlog_offset(log, log->head + entry_size);
		log->free += entry_size;

		/* update the read offset of all readers */
		list_for_each_entry(reader, &log->readers, list)
			if (reader->r_off == oldhead)
				reader->r_off = log->head;
	}

	/* write userlog header message */

	len = min(sizeof(header), log->size - log->w_off);
	memcpy(log->buffer + log->w_off, &header, len);
	memcpy(log->buffer, (char *)&header + len, sizeof(header) - len);

	/* get write offset for system message */
	w_off =  userlog_offset(log, log->w_off + sizeof(struct userlog_entry));

	/* write userlog system message */
	len = min(count, log->size - w_off);

	if (copy_from_iter(log->buffer + w_off, len, from) != len) {
		mutex_unlock(&log->mutex);
		return -EFAULT;
	}

	if (copy_from_iter(log->buffer, count - len, from) != count - len) {
		mutex_unlock(&log->mutex);
		return -EFAULT;
	}

	/* get write offset */
	log->w_off = userlog_offset(log, w_off + count);
	log->free -= sizeof(header) + count;

	mutex_unlock(&log->mutex);

	/* wake up any readers */
	wake_up_interruptible(&log->wq);

	return len;
}

/* userlog_open - the log's open() file operation */

static int userlog_open(struct inode *inode, struct file *file)
{
	struct userlog_reader *reader;

	reader = kmalloc(sizeof(struct userlog_reader), GFP_KERNEL);
	if (!reader)
		return -ENOMEM;
	reader->log = glog;
	mutex_lock(&glog->mutex);
	reader->r_off = glog->head;
	list_add(&reader->list, &glog->readers);
	mutex_unlock(&glog->mutex);
	file->private_data = reader;

	return 0;
}

/* userlog_release - the log's release file operation */

static int userlog_release(struct inode *ignored, struct file *file)
{
	struct userlog_reader *reader = file->private_data;

	mutex_lock(&glog->mutex);
	list_del(&reader->list);
	mutex_unlock(&glog->mutex);
	kfree(reader);

	pr_info("userlog_release\n");
	return 0;
}

static const struct file_operations userlog_fops = {
	.owner = THIS_MODULE,
	.read = userlog_read,
	.write_iter = userlog_write_iter,
	.open = userlog_open,
	.release = userlog_release,
};

/* create userlog init*/
static int __init create_userlog(char *log_name, int size)
{
	int ret = 0;
	unsigned char *buffer;

	/* vmalloc buffer size */
	buffer = vmalloc(size);
	if (buffer == NULL)
		return -ENOMEM;

	/* kzalloc userlog log */
	glog = kzalloc(sizeof(struct userlog_log), GFP_KERNEL);
	if (glog == NULL) {
		ret = -ENOMEM;
		goto out_free_buffer;
	}
	glog->buffer = buffer;

	glog->logdev.minor = MISC_DYNAMIC_MINOR;
	glog->logdev.name = kstrdup(log_name, GFP_KERNEL);
	if (glog->logdev.name == NULL) {
		ret = -ENOMEM;
		goto out_free_log;
	}

	/* init logdev fops */
	glog->logdev.fops = &userlog_fops;
	glog->logdev.parent = NULL;

	/* init wq */
	init_waitqueue_head(&glog->wq);
	/* init mutex */
	mutex_init(&glog->mutex);
	/* init list */
	INIT_LIST_HEAD(&glog->readers);

	/* init log w_off,head,size */
	glog->w_off = 0;
	glog->head = 0;
	glog->size = size;
	glog->free = size;

	/* register misc device for this log dev */
	ret = misc_register(&glog->logdev);
	if (unlikely(ret)) {
		pr_err("failed to register misc device for log '%s'!\n",
				glog->logdev.name);
		goto out_free_logdev_name;
	}

	pr_info("created %luK log '%s'\n",
			(unsigned long)glog->size >> 10, glog->logdev.name);

	return 0;

out_free_logdev_name:
	kfree(glog->logdev.name);

out_free_log:
	kfree(glog);

out_free_buffer:
	vfree(buffer);
	return ret;
}

/* module init */
static int __init unisoc_userlog_init(void)
{
	int ret;
#if true == ZTE_FEATURE_CGROUP_FREEZER_V2
	int ret_2;
	ret_2 = 0;

	pr_info("cpufreezer uevent init\n");

	ret_2 = class_register(&unfreeze_event_class);
	if (ret_2 < 0) {
		pr_err("cpufreezer unfreezer event: class_register failed!!!\n");
		return ret_2;
	}
	dev = device_create(&unfreeze_event_class, NULL, MKDEV(0, 0), NULL, "unfreezer");
	if (IS_ERR(dev)) {
		pr_err("cpufreezer:device_create failed!!!\n");
		ret_2 = IS_ERR(dev);
		goto unregister_class;
	}
	ret_2 = sysfs_create_group(&dev->kobj, &unfreeze_event_attr_group);
	if (ret_2 < 0) {
		pr_err("cpufreezer:sysfs_create_group failed!!!\n");
		goto destroy_device;
	}

	unfreeze_eventqueue = create_workqueue("send_unfreeze_event");
	if (unfreeze_eventqueue == NULL) {
		pr_err("unfreeze event module could not create workqueue!!!");
		ret_2 = -ENOMEM;
		goto destroy_device;
	}
	register_trace_android_vh_do_send_sig_info(signal_catch_for_freeze, NULL);
	register_trace_android_vh_binder_proc_transaction_finish(binder_catch_for_freeze_notify, NULL);
	register_trace_android_vh_binder_trans(binder_trans_for_freeze_notify, NULL);
	goto normal_class;

destroy_device:
	device_destroy(&unfreeze_event_class, MKDEV(0, 0));
unregister_class:
	class_unregister(&unfreeze_event_class);
normal_class:
#endif

	/* create userlog_system */
	pr_info("unisoc_userlog_init\n");
	ret = create_userlog(USERLOG_SYSTEM, 256*1024);
	return ret;
}

/* module exit */
static void __exit unisoc_userlog_exit(void)
{
	misc_deregister(&glog->logdev);
	vfree(glog->buffer);
	kfree(glog->logdev.name);
	kfree(glog);
	pr_info("unisoc_userlog_exit\n");
}

device_initcall(unisoc_userlog_init);
module_exit(unisoc_userlog_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhenguo.Zhao, <zhenguo.zhao1@unisoc.com>");
MODULE_DESCRIPTION("Unisoc User Log Driver");
