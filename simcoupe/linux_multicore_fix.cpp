// linux_multicore_fix.cpp — Multicore-safe replacements for Circle's linux compat
//
// Circle's addon/linux/ asserts Core 0 on all lock operations.
// We override with atomic versions that work from any core.
// Linked before liblinuxemu.a via --allow-multiple-definition.

#include <linux/rwlock.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <circle/synchronize.h>
#include <circle/sched/scheduler.h>

#define WRITE_LOCK	(1U << 31)

// ---- rwlock ----

void read_lock_bh (rwlock_t *lock)
{
	DataMemBarrier ();
	unsigned expected;
	do {
		expected = lock->lock;
		if (expected >= WRITE_LOCK) continue;
	} while (__sync_val_compare_and_swap (&lock->lock, expected, expected + 1) != expected);
	DataMemBarrier ();
}

void read_unlock_bh (rwlock_t *lock)
{
	DataMemBarrier ();
	__sync_fetch_and_sub (&lock->lock, 1);
	DataMemBarrier ();
}

void write_lock_bh (rwlock_t *lock)
{
	DataMemBarrier ();
	while (__sync_fetch_and_or (&lock->lock, WRITE_LOCK) & WRITE_LOCK)
		;
	while ((lock->lock & ~WRITE_LOCK) != 0)
		;
	DataMemBarrier ();
}

void write_unlock_bh (rwlock_t *lock)
{
	DataMemBarrier ();
	__sync_fetch_and_and (&lock->lock, ~WRITE_LOCK);
	DataMemBarrier ();
}

// ---- mutex ----

void mutex_lock (struct mutex *lock)
{
	while (__sync_lock_test_and_set (&lock->lock, 1))
		CScheduler::Get ()->Yield ();
	DataMemBarrier ();
}

void mutex_unlock (struct mutex *lock)
{
	DataMemBarrier ();
	__sync_lock_release (&lock->lock);
}

// ---- semaphore ----

void down (struct semaphore *sem)
{
	while (1)
	{
		DataMemBarrier ();
		if (__sync_fetch_and_sub (&sem->count.counter, 1) > 0)
			break;
		__sync_fetch_and_add (&sem->count.counter, 1);
		CScheduler::Get ()->Yield ();
	}
	DataMemBarrier ();
}

void up (struct semaphore *sem)
{
	DataMemBarrier ();
	__sync_fetch_and_add (&sem->count.counter, 1);
	DataMemBarrier ();
}

// ---- completion (all non-inline functions from completion.cpp) ----

#include <linux/completion.h>
#include <linux/jiffies.h>

void complete (struct completion *x)
{
	DataMemBarrier ();
	__sync_fetch_and_add (&x->done, 1);
	DataMemBarrier ();
}

void complete_all (struct completion *x)
{
	DataMemBarrier ();
	x->done = (unsigned) -1 / 2;
	DataMemBarrier ();
}

void wait_for_completion (struct completion *x)
{
	while (1)
	{
		DataMemBarrier ();
		if (x->done > 0)
		{
			__sync_fetch_and_sub (&x->done, 1);
			break;
		}
		CScheduler::Get ()->Yield ();
	}
}

int try_wait_for_completion (struct completion *x)
{
	DataMemBarrier ();
	if (x->done == 0)
		return 0;
	__sync_fetch_and_sub (&x->done, 1);
	return 1;
}

unsigned long wait_for_completion_timeout (struct completion *x, unsigned long timeout)
{
	unsigned long start = jiffies;
	while (1)
	{
		DataMemBarrier ();
		if (x->done > 0)
		{
			__sync_fetch_and_sub (&x->done, 1);
			return 1;
		}
		if (jiffies - start >= timeout)
			return 0;
		CScheduler::Get ()->Yield ();
	}
}

long wait_for_completion_interruptible_timeout (struct completion *x, unsigned long timeout)
{
	return (long) wait_for_completion_timeout (x, timeout);
}
