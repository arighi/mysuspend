/*
 * mysuspend: Android power management example
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Copyright (C) 2012 Andrea Righi <andrea@betterlinux.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/time.h>

#define MY_DBG(fmt, args...)	\
		printk(KERN_INFO "[mysuspend] "fmt, ## args);

/* Enable/disable the different features in this module */
#define ENABLE_MY_DELAYED_WORK
#define ENABLE_MY_TIMER
#define ENABLE_MY_WAKE_LOCK
#define ENABLE_MY_ALARM
#define ENABLE_MY_PM_NOTIFIER
#define ENABLE_MY_EARLY_SUSPEND

/* Helper function to return the timestamp in seconds (from the RTC) */
static inline unsigned long get_my_seconds(void)
{
	struct timespec now;

	getnstimeofday(&now);
	return now.tv_sec;
}

/*** Delayed work interface: BEGIN ***/
#ifdef ENABLE_MY_DELAYED_WORK
#include <linux/workqueue.h>

#define WORK_PERIOD_MS	(1ULL * MSEC_PER_SEC)

static void my_delayed_work_handler(struct work_struct *work);

static DECLARE_DELAYED_WORK(my_delayed_work, my_delayed_work_handler);

static void my_delayed_work_start(void)
{
	schedule_delayed_work(&my_delayed_work,
			msecs_to_jiffies(WORK_PERIOD_MS));
}

static void my_delayed_work_stop(void)
{
	cancel_delayed_work_sync(&my_delayed_work);
}

static void my_delayed_work_handler(struct work_struct *work)
{
	MY_DBG("%s: %lu\n", __func__, get_my_seconds());
	my_delayed_work_start();
}
#else
static inline void my_delayed_work_start(void) { }
static inline void my_delayed_work_stop(void) { }
#endif
/*** Delayed work interface: END ***/

/*** Timer interface: BEGIN ***/
#ifdef ENABLE_MY_TIMER
#include <linux/timer.h>

#define TIMER_PERIOD_MS	(1ULL * MSEC_PER_SEC)

static void my_timer_handler(unsigned long __data);

static DEFINE_TIMER(my_timer, my_timer_handler, 0, 0);

static void my_timer_start(void)
{
	mod_timer(&my_timer, jiffies + msecs_to_jiffies(TIMER_PERIOD_MS));
}

static void my_timer_handler(unsigned long __data)
{
	MY_DBG("%s: %lu\n", __func__, get_my_seconds());
	my_timer_start();
}

static void my_timer_stop(void)
{
	del_timer_sync(&my_timer);
}
#else
static inline void my_timer_start(void) { }
static inline void my_timer_stop(void) { }
#endif
/*** Timer interface: END ***/

/*** Alarm: BEGIN ***/
#ifdef ENABLE_MY_ALARM
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/android_alarm.h>

#define ALARM_PERIOD_MS	(10ULL * MSEC_PER_SEC)

static struct alarm my_alarm;

static void my_alarm_shot(void)
{
	ktime_t now, expire;

	now = ktime_get_real();
	expire = ktime_add(now,
			ns_to_ktime((u64)ALARM_PERIOD_MS * NSEC_PER_MSEC));

        alarm_start_range(&my_alarm, expire, expire);
}

static void my_alarm_handler(struct alarm *alarm)
{
	MY_DBG("%s: %lu\n", __func__, get_my_seconds());
	my_alarm_shot();
}

static void my_alarm_start(void)
{
	alarm_init(&my_alarm, ANDROID_ALARM_RTC_WAKEUP, my_alarm_handler);
	my_alarm_shot();
}

static void my_alarm_stop(void)
{
	alarm_cancel(&my_alarm);
}
#else
static inline void my_alarm_start(void) { }
static inline void my_alarm_stop(void) { }
#endif
/*** Alarm: END ***/

/*** pm notifier: BEGIN ***/
#ifdef ENABLE_MY_PM_NOTIFIER
#include <linux/suspend.h>

static int my_pm_handler(struct notifier_block *nfb,
			unsigned long action, void *data)
{
	switch (action) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		MY_DBG("%s: suspend\n", __func__);
		return NOTIFY_OK;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		MY_DBG("%s: resume\n", __func__);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static struct notifier_block my_pm_notifier = {
	.notifier_call = my_pm_handler,
};

static void my_pm_notifier_start(void)
{
	register_pm_notifier(&my_pm_notifier);
}

static void my_pm_notifier_stop(void)
{
	unregister_pm_notifier(&my_pm_notifier);
}
#else
static inline void my_pm_notifier_start(void) { }
static inline void my_pm_notifier_stop(void) { }
#endif
/*** pm notifier: END ***/

/*** early suspend: BEGIN ***/
#ifdef ENABLE_MY_EARLY_SUSPEND
#include <linux/earlysuspend.h>

static void my_early_suspend_handler(struct early_suspend *handler)
{
	MY_DBG("%s\n", __func__);
}

static void my_early_resume_handler(struct early_suspend *handler)
{
	MY_DBG("%s\n", __func__);
}

/*
 * The early-suspend API allows drivers to get notified when user-space writes
 * to /sys/power/state to indicate that the user visible sleep state should
 * change. Suspend handlers are called in order of low to high (4 - 1 below)
 * and resume handlers are called in order of high to low (1 - 4 below).
 *
 * EARLY_SUSPEND_LEVEL_BLANK_SCREEN:
 *	on suspend: the screen should be turned off but the framebuffer must
 *	still be accessible
 *	on resume: the screen can be turned back on.
 *
 * EARLY_SUSPEND_LEVEL_STOP_DRAWING:
 *	on suspend: this level notifies user-space that it should stop
 *	accessing the framebuffer and it waits for it to complete.
 *
 *	on resume: it notifies user-space that it should resume screen access.
 *	Two methods are provided, console switch or a sysfs interface.
 *
 * EARLY_SUSPEND_LEVEL_DISABLE_FB: Turn off the framebuffer
 *	on suspend: turn off the framebuffer
 *	on resume: turn the framebuffer back on.
 */
static struct early_suspend my_early_suspend = {
	.suspend = my_early_suspend_handler,
	.resume = my_early_resume_handler,
	.level = EARLY_SUSPEND_LEVEL_DISABLE_FB,
};

static void my_early_suspend_start(void)
{
	register_early_suspend(&my_early_suspend);
}

static void my_early_suspend_stop(void)
{
	unregister_early_suspend(&my_early_suspend);
}
#else
static inline void my_early_suspend_start(void) { }
static inline void my_early_suspend_stop(void) { }
#endif
/*** early suspend: END ***/

/*** Wake lock: BEGIN ***/
#ifdef ENABLE_MY_WAKE_LOCK
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/wakelock.h>

static struct wake_lock my_wake_lock;

static void my_wake_lock_start(void)
{
	wake_lock_init(&my_wake_lock, WAKE_LOCK_SUSPEND, "my_wake_lock");
	wake_lock(&my_wake_lock);
}

static void my_wake_lock_stop(void)
{
	wake_unlock(&my_wake_lock);
	wake_lock_destroy(&my_wake_lock);
}
#else
static inline void my_wake_lock_start(void) { }
static inline void my_wake_lock_stop(void) { }
#endif
/*** Wake lock: END ***/

/*** Module entry/exit point: BEGIN ***/
static int __init my_init(void)
{
	my_wake_lock_start();
	my_pm_notifier_start();
	my_early_suspend_start();
	my_delayed_work_start();
	my_timer_start();
	my_alarm_start();

	return 0;
}

static void __exit my_exit(void)
{
	my_alarm_stop();
	my_timer_stop();
	my_delayed_work_stop();
	my_pm_notifier_stop();
	my_early_suspend_stop();
	my_wake_lock_stop();
}

module_init(my_init);
module_exit(my_exit);
/*** Module entry/exit point: END ***/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Righi <andrea@betterlinux.com>");
MODULE_DESCRIPTION("Android power management example");
