/**
 * @file
 *
 * @ingroup rtems_bsd_machine
 *
 * @brief Implementation of a mutex with a simple priority inheritance
 * protocol.
 */

/*
 * Copyright (c) 2014 embedded brains GmbH.  All rights reserved.
 *
 *  embedded brains GmbH
 *  Dornierstr. 4
 *  82178 Puchheim
 *  Germany
 *  <rtems@embedded-brains.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _RTEMS_BSD_MACHINE_RTEMS_BSD_MUTEXIMPL_H_
#define _RTEMS_BSD_MACHINE_RTEMS_BSD_MUTEXIMPL_H_

#include <machine/rtems-bsd-mutex.h>
#include <machine/rtems-bsd-support.h>

#include <rtems/bsd/sys/types.h>
#include <rtems/bsd/sys/lock.h>

#include <rtems/score/isrlevel.h>
#include <rtems/score/threadimpl.h>
#include <rtems/score/threadqimpl.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

static inline void
rtems_bsd_mutex_init(struct lock_object *lock, rtems_bsd_mutex *m,
    struct lock_class *class, const char *name, const char *type, int flags)
{
	m->owner = NULL;
	m->nest_level = 0;
	_RBTree_Initialize_empty(&m->rivals);

	lock_init(lock, class, name, type, flags);
}

void rtems_bsd_mutex_lock_more(struct lock_object *lock, rtems_bsd_mutex *m,
    Thread_Control *owner, Thread_Control *executing, ISR_Level level);

static inline void
rtems_bsd_mutex_lock(struct lock_object *lock, rtems_bsd_mutex *m)
{
	ISR_Level level;
	Thread_Control *executing;
	Thread_Control *owner;

	_ISR_Disable(level);

	owner = m->owner;
	executing = _Thread_Executing;

	if (__predict_true(owner == NULL)) {
		m->owner = executing;
		++executing->resource_count;

		_ISR_Enable(level);
	} else {
		rtems_bsd_mutex_lock_more(lock, m, owner, executing, level);
	}
}

static inline int
rtems_bsd_mutex_trylock(struct lock_object *lock, rtems_bsd_mutex *m)
{
	int success;
	ISR_Level level;
	Thread_Control *executing;
	Thread_Control *owner;

	_ISR_Disable(level);

	owner = m->owner;
	executing = _Thread_Executing;

	if (owner == NULL) {
		m->owner = executing;
		++executing->resource_count;
		success = 1;
	} else if (owner == executing) {
		BSD_ASSERT(lock->lo_flags & LO_RECURSABLE);
		++m->nest_level;
		success = 1;
	} else {
		success = 0;
	}

	_ISR_Enable(level);

	return (success);
}

void rtems_bsd_mutex_unlock_more(rtems_bsd_mutex *m, Thread_Control *owner,
    int keep_priority, RBTree_Node *first, ISR_Level level);

static inline void
rtems_bsd_mutex_unlock(rtems_bsd_mutex *m)
{
	ISR_Level level;
	int nest_level;

	_ISR_Disable(level);

	nest_level = m->nest_level;
	if (__predict_true(nest_level == 0)) {
		RBTree_Node *first = _RBTree_First(&m->rivals, RBT_LEFT);
		Thread_Control *owner = m->owner;
		int keep_priority;

		--owner->resource_count;
		keep_priority = _Thread_Owns_resources(owner)
		    || owner->real_priority == owner->current_priority;

		m->owner = NULL;

		if (__predict_true(first == NULL && keep_priority
		    && owner == _Thread_Executing)) {
			_ISR_Enable(level);
		} else {
			rtems_bsd_mutex_unlock_more(m, owner, keep_priority,
			    first, level);
		}

	} else {
		m->nest_level = nest_level - 1;

		_ISR_Enable(level);
	}
}

static inline int
rtems_bsd_mutex_owned(rtems_bsd_mutex *m)
{

	return (m->owner == _Thread_Get_executing());
}

static inline int
rtems_bsd_mutex_recursed(rtems_bsd_mutex *m)
{

	return (m->nest_level);
}

static inline void
rtems_bsd_mutex_destroy(struct lock_object *lock, rtems_bsd_mutex *m)
{
	BSD_ASSERT(_RBTree_Is_empty(&m->rivals));

	if (rtems_bsd_mutex_owned(m)) {
		m->nest_level = 0;
		rtems_bsd_mutex_unlock(m);
	}

	lock_destroy(lock);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _RTEMS_BSD_MACHINE_RTEMS_BSD_MUTEXIMPL_H_ */