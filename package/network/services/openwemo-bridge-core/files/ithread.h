#ifndef OPENWEMO_ITHREAD_COMPAT_H
#define OPENWEMO_ITHREAD_COMPAT_H

#include <pthread.h>
#include <unistd.h>

typedef pthread_t ithread_t;
typedef pthread_attr_t ithread_attr_t;
typedef pthread_cond_t ithread_cond_t;
typedef pthread_condattr_t ithread_condattr_t;
typedef pthread_mutex_t ithread_mutex_t;
typedef pthread_mutexattr_t ithread_mutexattr_t;

#define ITHREAD_CANCELED PTHREAD_CANCELED
#define ITHREAD_CREATE_DETACHED PTHREAD_CREATE_DETACHED
#define ITHREAD_CREATE_JOINABLE PTHREAD_CREATE_JOINABLE
#define ITHREAD_MUTEX_ERRORCHECK_NP PTHREAD_MUTEX_ERRORCHECK
#define ITHREAD_MUTEX_FAST_NP PTHREAD_MUTEX_NORMAL
#define ITHREAD_MUTEX_RECURSIVE_NP PTHREAD_MUTEX_RECURSIVE
#define ITHREAD_STACK_MIN PTHREAD_STACK_MIN

#define ithread_attr_destroy pthread_attr_destroy
#define ithread_attr_init pthread_attr_init
#define ithread_attr_setdetachstate pthread_attr_setdetachstate
#define ithread_attr_setstacksize pthread_attr_setstacksize
#define ithread_cancel pthread_cancel
#define ithread_cond_broadcast pthread_cond_broadcast
#define ithread_cond_destroy pthread_cond_destroy
#define ithread_cond_init pthread_cond_init
#define ithread_cond_signal pthread_cond_signal
#define ithread_cond_timedwait pthread_cond_timedwait
#define ithread_cond_wait pthread_cond_wait
#define ithread_create pthread_create
#define ithread_detach pthread_detach
#define ithread_exit pthread_exit
#define ithread_get_current_thread_id pthread_self
#define ithread_join pthread_join
#define ithread_mutex_destroy pthread_mutex_destroy
#define ithread_mutex_init pthread_mutex_init
#define ithread_mutex_lock pthread_mutex_lock
#define ithread_mutex_unlock pthread_mutex_unlock
#define ithread_mutexattr_destroy pthread_mutexattr_destroy
#define ithread_mutexattr_getkind_np pthread_mutexattr_gettype
#define ithread_mutexattr_gettype pthread_mutexattr_gettype
#define ithread_mutexattr_init pthread_mutexattr_init
#define ithread_mutexattr_setkind_np pthread_mutexattr_settype
#define ithread_mutexattr_settype pthread_mutexattr_settype
#define ithread_self pthread_self

#define isleep sleep
#define imillisleep(ms) usleep((ms) * 1000)

static inline int ithread_cleanup_library(void)
{
	return 0;
}

static inline int ithread_cleanup_thread(void)
{
	return 0;
}

static inline int ithread_initialize_library(void)
{
	return 0;
}

static inline int ithread_initialize_thread(void)
{
	return 0;
}

#endif
