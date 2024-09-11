#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sched.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include "gt_include.h"
#include "gt_pq.c"
#include <unistd.h>  
/**********************************************************************/
/** DECLARATIONS **/
/**********************************************************************/

#define CREDIT_SCHED 
#define PRINTFROM_CPU 0
/**********************************************************************/
/* kthread runqueue and env */

/* XXX: should be the apic-id */
#define KTHREAD_CUR_ID	0
#define NUM_CREDITS_PER_MS 2.0
/**********************************************************************/
/* uthread scheduling */
static void uthread_context_func(int);
static int uthread_init(uthread_struct_t *u_new);

/**********************************************************************/
/* uthread creation */
#define UTHREAD_DEFAULT_SSIZE (16 * 1024)

extern int uthread_create(uthread_t *u_tid, int (*u_func)(void *), void *u_arg, uthread_group_t u_gid, int credit);

/**********************************************************************/
/** DEFNITIONS **/
/**********************************************************************/

/**********************************************************************/
/* uthread scheduling */
void print_credit(int in_out){
for(int i = 0; i < 4; i++){
		fprintf(stderr, "______________PRINTING INFO ON CPU %d AT PQ FILE!!!_____________________________\n", i);
		kthread_context_t *k_ctx = kthread_cpu_map[i];
		uthread_struct_t *u_obj;
		gt_spin_lock(&(k_ctx->krunqueue.kthread_runqlock));
		uthread_head_t *uthread_head = (k_ctx->krunqueue.active_credit_tracker);
		if(k_ctx->krunqueue.active_runq->uthread_tot != 0){
			fprintf(stderr, "active q: %d[", k_ctx->krunqueue.active_runq->uthread_tot);
			TAILQ_FOREACH(u_obj, uthread_head, uthread_creditq) {
				fprintf(stderr, "A%d(c=%d), ", u_obj->uthread_tid,u_obj->credit);
				
			}
			fprintf(stderr, "] \n");
		}

		uthread_head = (k_ctx->krunqueue.expired_credit_tracker);
		if(k_ctx->krunqueue.expires_runq->uthread_tot != 0){
			fprintf(stderr, "expired q: %d[", k_ctx->krunqueue.expires_runq->uthread_tot);
			TAILQ_FOREACH(u_obj, uthread_head, uthread_creditq) {
				fprintf(stderr, "E%d(c=%d), ", u_obj->uthread_tid,u_obj->credit);
				
			}
			fprintf(stderr, "] \n");
		}
		gt_spin_unlock(&(k_ctx->krunqueue.kthread_runqlock));
	}
}
/* Assumes that the caller has disabled vtalrm and sigusr1 signals */
/* uthread_init will be using */
static int uthread_init(uthread_struct_t *u_new)
{
	stack_t oldstack;
	sigset_t set, oldset;
	struct sigaction act, oldact;

	gt_spin_lock(&(ksched_shared_info.uthread_init_lock));

	/* Register a signal(SIGUSR2) for alternate stack */
	act.sa_handler = uthread_context_func;
	act.sa_flags = (SA_ONSTACK | SA_RESTART);
	if(sigaction(SIGUSR2,&act,&oldact))
	{
		fprintf(stderr, "uthread sigusr2 install failed !!");
		return -1;
	}

	/* Install alternate signal stack (for SIGUSR2) */
	if(sigaltstack(&(u_new->uthread_stack), &oldstack))
	{
		fprintf(stderr, "uthread sigaltstack install failed.");
		return -1;
	}

	/* Unblock the signal(SIGUSR2) */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_UNBLOCK, &set, &oldset);


	/* SIGUSR2 handler expects kthread_runq->cur_uthread
	 * to point to the newly created thread. We will temporarily
	 * change cur_uthread, before entering the synchronous call
	 * to SIGUSR2. */

	/* kthread_runq is made to point to this new thread
	 * in the caller. Raise the signal(SIGUSR2) synchronously */
#if 0
	raise(SIGUSR2);
#endif
	syscall(__NR_tkill, kthread_cpu_map[kthread_apic_id()]->tid, SIGUSR2);

	/* Block the signal(SIGUSR2) */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_BLOCK, &set, &oldset);
	if(sigaction(SIGUSR2,&oldact,NULL))
	{
		fprintf(stderr, "uthread sigusr2 revert failed !!");
		return -1;
	}

	/* Disable the stack for signal(SIGUSR2) handling */
	u_new->uthread_stack.ss_flags = SS_DISABLE;

	/* Restore the old stack/signal handling */
	if(sigaltstack(&oldstack, NULL))
	{
		fprintf(stderr, "uthread sigaltstack revert failed.");
		return -1;
	}

	gt_spin_unlock(&(ksched_shared_info.uthread_init_lock));
	return 0;
}
// start uthread tiimer when uthread start running
void uthread_start_timer(uthread_struct_t *u_obj) {
    clock_gettime(CLOCK_MONOTONIC, &(u_obj->start_time));
}
void uthread_stop_timer(uthread_struct_t *u_obj) {
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    // Calculate the elapsed time in milliseconds
    double elapsed_ms = (end_time.tv_sec - u_obj->start_time.tv_sec) * 1000.0 + 
                        (end_time.tv_nsec - u_obj->start_time.tv_nsec) / 1000000.0;

    // Deduct credit: 1 credit for every 10 ms
	double credits_used = (int)((double)(elapsed_ms * 10.0));
    u_obj->credit -= (int)(credits_used);
	
	if(u_obj->uthread_state == UTHREAD_RUNNING) 
    fprintf(stderr, "Thread %d ran for %f ms on k %d, status: running, deducted %f. Remaining: %d\n",
           u_obj->uthread_tid,elapsed_ms, u_obj->cpu_id,credits_used, u_obj->credit);
	else if(u_obj->uthread_state == UTHREAD_DONE || u_obj->uthread_state == UTHREAD_CANCELLED)
	    fprintf(stderr, "Thread %dran for %f ms on k %d, status: done, deducted %f. Remaining: %d\n",
           u_obj->uthread_tid,elapsed_ms,  u_obj->cpu_id,credits_used, u_obj->credit);
	uthread_start_timer(u_obj);
}
int id = 0;
extern void credit_scheduler(uthread_struct_t * (*credit_find_best_uthread)(kthread_runqueue_t *))
{
	// usleep(5000); // to avoid race condition
	if(kthread_apic_id() == PRINTFROM_CPU){
		// print_credit(1);
	}
	// usleep(1000); 
	kthread_context_t *k_ctx;
	kthread_runqueue_t *kthread_runq;
	uthread_struct_t *u_obj;

	/* Signals used for cpu_thread scheduling */
	kthread_block_signal(SIGVTALRM);
	kthread_block_signal(SIGUSR1);
	
	// if(kthread_apic_id() == PRINTFROM_CPU){
	// 	fprintf(stderr, "\n\n!!!!!!!!!credit_scheduler invoked !!!!!!!!!!! in kthread %d\n", kthread_apic_id());
	// 	print_credit();
	// }

	k_ctx = kthread_cpu_map[kthread_apic_id()];
	kthread_runq = &(k_ctx->krunqueue);
	// if currently running, 
	// 1. check if current thread is cancelled or done, if so, put it to zombie queue and remove from credit queue
	// 2. stop timer and check current credit
	// 		if credit is still over 0, return current thread to keep running, start timer when keep running
	// 		 if credit is less than 0, run state as runnable and move it to expired runq, move from active credit tracker to exired credit tracker
	// 		find next thread to run and start tiemr for it
	u_obj = kthread_runq->cur_uthread;
	if((u_obj))
	{
		fprintf(stderr,"into %d /n",u_obj->uthread_state);
		// check if the current one is cancelled or done, if so, put it into zombie queue and remove from credit queue */
		if(u_obj->uthread_state & (UTHREAD_DONE | UTHREAD_CANCELLED))
		{
			fprintf(stderr, "Thread %d in kthread %d is done, kthread active q: (%d), kthread expired q: (%d)\n", 
			    u_obj->uthread_tid, kthread_apic_id(), kthread_runq->active_runq->uthread_tot , kthread_runq->expires_runq->uthread_tot);
			kthread_runq->cur_uthread = NULL;
			/* XXX: Inserting uthread into zombie queue is causing improper
			 * cleanup/exit of uthread (core dump) */
			uthread_head_t * kthread_zhead = &(kthread_runq->zombie_uthreads);
			uthread_head_t * credit_head = (kthread_runq->active_credit_tracker);
			gt_spin_lock(&(kthread_runq->kthread_runqlock));
			kthread_runq->kthread_runqlock.holder = 0x01;
			TAILQ_INSERT_TAIL(kthread_zhead, u_obj, uthread_runq);
			gt_spin_unlock(&(kthread_runq->kthread_runqlock));

			{
				ksched_shared_info_t *ksched_info = &ksched_shared_info;	
				gt_spin_lock(&ksched_info->ksched_lock);
				ksched_info->kthread_cur_uthreads--;
				gt_spin_unlock(&ksched_info->ksched_lock);
			}
		}
		// if current trhead is still runnable */
		else
		{
			fprintf(stderr,"into this weird se/n");
			uthread_stop_timer(u_obj);
			// if credit is still over 0, return current thread to keep running, start timer when keep running
			if(u_obj->credit > 0){
                return;
			}
			// if credit is less = than 0, run state as runnable and move it to expired runq, 
			// move from active credit tracker to exired credit tracker
			else{
				// if(kthread_apic_id() == PRINTFROM_CPU)
				// 	fprintf(stderr, "Case: Credit less than 0, put to runnable and move to expired queue\n");
				kthread_runq->cur_uthread = NULL;
				u_obj->uthread_state = UTHREAD_RUNNABLE;

				add_to_runqueue(kthread_runq->expires_runq, &(kthread_runq->kthread_runqlock), u_obj);
			
				// move current obj to expied credit tracker
				gt_spin_lock(&(kthread_runq->kthread_runqlock));
				TAILQ_INSERT_TAIL( (kthread_runq->expired_credit_tracker), u_obj, uthread_creditq);
				gt_spin_unlock(&(kthread_runq->kthread_runqlock));

				fprintf(stderr, "Thread %d in kthread %d moved to expired, kthread active q: (%d), kthread expired q: (%d)\n", 
					u_obj->uthread_tid, u_obj->cpu_id, kthread_runq->active_runq->uthread_tot , kthread_runq->expires_runq->uthread_tot);

				// if(kthread_apic_id() == PRINTFROM_CPU){
				// 	fprintf(stderr, "after put to runnable and move to expired queue: %d", kthread_runq->expires_runq->uthread_tot);
				// 	print_credit(0);
				// }
				/* XXX: Save the context (signal mask not saved) */
				if(sigsetjmp(u_obj->uthread_env, 0))
					return;
			}
		}
	}
	/* kthread_best_sched_uthread acquires kthread_runqlock. Dont lock it up when calling the function. */
	// if tehre's no next best thread to get, return to main */
	u_obj = NULL;
	if(!(u_obj = credit_find_best_uthread(kthread_runq)))
	{
		fprintf(stderr, "no uthread in kthread %d to be run, kthread active q: (%d), kthread expired q: (%d)\n", 
		 kthread_apic_id(), kthread_runq->active_runq->uthread_tot , kthread_runq->expires_runq->uthread_tot);

		// fprintf(stderr, "No next best thread to get, total %d\n",ksched_shared_info.kthread_cur_uthreads);
		/* Done executing all uthreads. Return to main */
		/* XXX: We can actually get rid of KTHREAD_DONE flag */
		if(ksched_shared_info.kthread_tot_uthreads && !ksched_shared_info.kthread_cur_uthreads)
		{
			fprintf(stderr, "Quitting kthread (%d)\n", k_ctx->cpuid);
			k_ctx->kthread_flags |= KTHREAD_DONE;
		}

		kthread_unblock_signal(SIGVTALRM);
		kthread_unblock_signal(SIGUSR1);
		siglongjmp(k_ctx->kthread_env, 1);
		return;
	}
	fprintf(stderr, "Thread %d in kthread %d is put to run, kthread active q: (%d), kthread expired q: (%d)\n", 
	u_obj->uthread_tid, u_obj->cpu_id, kthread_runq->active_runq->uthread_tot , kthread_runq->expires_runq->uthread_tot);

	kthread_runq->cur_uthread = u_obj;
	if((u_obj->uthread_state == UTHREAD_INIT) && (uthread_init(u_obj)))
	{
		fprintf(stderr, "uthread_init failed on kthread(%d)\n", k_ctx->cpuid);
		exit(0);
	}

	u_obj->uthread_state = UTHREAD_RUNNING;
	uthread_start_timer(u_obj);
	kthread_unblock_signal(SIGVTALRM);
	kthread_unblock_signal(SIGUSR1);
	/* Re-install the scheduling signal handlers */
	kthread_install_sighandler(SIGVTALRM, k_ctx->kthread_sched_timer);
	kthread_install_sighandler(SIGUSR1, k_ctx->kthread_sched_relay);

	/* Jump to the selected uthread context */
	siglongjmp(u_obj->uthread_env, 1);
	
	return;
}

extern void uthread_schedule(uthread_struct_t * (*kthread_best_sched_uthread)(kthread_runqueue_t *))
{
	kthread_context_t *k_ctx;
	kthread_runqueue_t *kthread_runq;
	uthread_struct_t *u_obj;

	/* Signals used for cpu_thread scheduling */
	// kthread_block_signal(SIGVTALRM);
	// kthread_block_signal(SIGUSR1);
	// print_credit(k_ctx);
#if 0
	fprintf(stderr, "uthread_schedule invoked !!\n");
#endif

	k_ctx = kthread_cpu_map[kthread_apic_id()];
	kthread_runq = &(k_ctx->krunqueue);
	// if currently running
	if((u_obj = kthread_runq->cur_uthread))
	{
		/*Go through the runq and schedule the next thread to run */
		kthread_runq->cur_uthread = NULL;
		// check if the current one is cancelled or done, if so, put it into zombie queue */
		if(u_obj->uthread_state & (UTHREAD_DONE | UTHREAD_CANCELLED))
		{
			/* XXX: Inserting uthread into zombie queue is causing improper
			 * cleanup/exit of uthread (core dump) */
			uthread_head_t * kthread_zhead = &(kthread_runq->zombie_uthreads);
			gt_spin_lock(&(kthread_runq->kthread_runqlock));
			kthread_runq->kthread_runqlock.holder = 0x01;
			TAILQ_INSERT_TAIL(kthread_zhead, u_obj, uthread_runq);
			gt_spin_unlock(&(kthread_runq->kthread_runqlock));

			{
				ksched_shared_info_t *ksched_info = &ksched_shared_info;	
				gt_spin_lock(&ksched_info->ksched_lock);
				ksched_info->kthread_cur_uthreads--;
				gt_spin_unlock(&ksched_info->ksched_lock);
			}
		}
		// if cuurrent trhead is still runnable, move it to expires runq and save context for later runf */
		else
		{
			/* XXX: Apply uthread_group_penalty before insertion */
			u_obj->uthread_state = UTHREAD_RUNNABLE;
			add_to_runqueue(kthread_runq->expires_runq, &(kthread_runq->kthread_runqlock), u_obj);
			/* XXX: Save the context (signal mask not saved) */
			if(sigsetjmp(u_obj->uthread_env, 0))
				return;
		}
	}
//  usleep(5000);  
	/* kthread_best_sched_uthread acquires kthread_runqlock. Dont lock it up when calling the function. */
	// if tehre's no next best thread to get, return to main */
	if(!(u_obj = kthread_best_sched_uthread(kthread_runq)))
	{
		/* Done executing all uthreads. Return to main */
		/* XXX: We can actually get rid of KTHREAD_DONE flag */
		if(ksched_shared_info.kthread_tot_uthreads && !ksched_shared_info.kthread_cur_uthreads)
		{
			fprintf(stderr, "Quitting kthread (%d)\n", k_ctx->cpuid);
			k_ctx->kthread_flags |= KTHREAD_DONE;
		}
		
		siglongjmp(k_ctx->kthread_env, 1);
		return;
	}
	fprintf(stderr, "next thread found: %d\n", u_obj->uthread_tid);

	kthread_runq->cur_uthread = u_obj;
	if((u_obj->uthread_state == UTHREAD_INIT) && (uthread_init(u_obj)))
	{
		fprintf(stderr, "uthread_init failed on kthread(%d)\n", k_ctx->cpuid);
		exit(0);
	}

	u_obj->uthread_state = UTHREAD_RUNNING;

	/* Re-install the scheduling signal handlers */
	kthread_install_sighandler(SIGVTALRM, k_ctx->kthread_sched_timer);
	kthread_install_sighandler(SIGUSR1, k_ctx->kthread_sched_relay);
	/* Jump to the selected uthread context */
	siglongjmp(u_obj->uthread_env, 1);

	return;
}


/* For uthreads, we obtain a seperate stack by registering an alternate
 * stack for SIGUSR2 signal. Once the context is saved, we turn this 
 * into a regular stack for uthread (by using SS_DISABLE). */
static void uthread_context_func(int signo)
{
	usleep(10000);
	uthread_struct_t *cur_uthread;
	kthread_runqueue_t *kthread_runq;

	kthread_runq = &(kthread_cpu_map[kthread_apic_id()]->krunqueue);

	// fprintf(stderr, ".!!!!!!!!!!.... UTHREAD JOB RUNNING NOW.!!!!!!!!..., in kthread %d\n", kthread_apic_id());
	/* kthread->cur_uthread points to newly created uthread */
	if(!sigsetjmp(kthread_runq->cur_uthread->uthread_env,0))
	{
		/* In UTHREAD_INIT : saves the context and returns.
		 * Otherwise, continues execution. */
		/* DONT USE any locks here !! */
		assert(kthread_runq->cur_uthread->uthread_state == UTHREAD_INIT);
		kthread_runq->cur_uthread->uthread_state = UTHREAD_RUNNABLE;
		return;
	}

	/* UTHREAD_RUNNING : siglongjmp was executed. */
	cur_uthread = kthread_runq->cur_uthread;
	assert(cur_uthread->uthread_state == UTHREAD_RUNNING);
	/* Execute the uthread task */
	cur_uthread->uthread_func(cur_uthread->uthread_arg);
	uthread_stop_timer(cur_uthread);
	cur_uthread->uthread_state = UTHREAD_DONE;
	

	// uthread_schedule(&sched_find_best_uthread);
	#ifdef CREDIT_SCHED
		credit_scheduler(&credit_find_best_uthread);
	#else
		uthread_schedule(&sched_find_best_uthread);
	#endif
	return;
}

/**********************************************************************/
/* uthread creation */

extern kthread_runqueue_t *ksched_find_target(uthread_struct_t *);

extern int uthread_create(uthread_t *u_tid, int (*u_func)(void *), void *u_arg, uthread_group_t u_gid, int credit)
{
	kthread_runqueue_t *kthread_runq;
	uthread_struct_t *u_new;

	/* Signals used for cpu_thread scheduling */
	// kthread_block_signal(SIGVTALRM);
	// kthread_block_signal(SIGUSR1);

	/* create a new uthread structure and fill it */
	if(!(u_new = (uthread_struct_t *)MALLOCZ_SAFE(sizeof(uthread_struct_t))))
	{
		fprintf(stderr, "uthread mem alloc failure !!");
		exit(0);
	}
	
	u_new->uthread_state = UTHREAD_INIT ;
	u_new->uthread_priority = DEFAULT_UTHREAD_PRIORITY;
	u_new->uthread_gid = 1;
	u_new->uthread_func = u_func;
	u_new->uthread_arg = u_arg;
	u_new->init_credit = credit;
	u_new->credit = credit;

	/* Allocate new stack for uthread */
	u_new->uthread_stack.ss_flags = 0; /* Stack enabled for signal handling */
	if(!(u_new->uthread_stack.ss_sp = (void *)MALLOC_SAFE(UTHREAD_DEFAULT_SSIZE)))
	{
		fprintf(stderr, "uthread stack mem alloc failure !!");
		return -1;
	}
	u_new->uthread_stack.ss_size = UTHREAD_DEFAULT_SSIZE;


	{
		ksched_shared_info_t *ksched_info = &ksched_shared_info;

		gt_spin_lock(&ksched_info->ksched_lock);
		u_new->uthread_tid = ksched_info->kthread_tot_uthreads++;
		ksched_info->kthread_cur_uthreads++; 
		gt_spin_unlock(&ksched_info->ksched_lock);  
	}

	/* XXX: ksched_find_target should be a function pointer */
	kthread_runq = ksched_find_target(u_new);
 
	*u_tid = u_new->uthread_tid;
	/* Queue the uthread for target-cpu. Let target-cpu take care of initialization. */
	add_to_runqueue(kthread_runq->active_runq, &(kthread_runq->kthread_runqlock), u_new);
	gt_spinlock_t *runq_lock = &(kthread_runq->kthread_runqlock);
	gt_spin_lock(runq_lock);
	runq_lock->holder = 0x02;
	
	// fprintf(stderr, "current runq: %d\n", kthread_runq->active_runq->uthread_tot);
	if(TAILQ_EMPTY((kthread_runq->active_credit_tracker)))
		TAILQ_INIT((kthread_runq->active_credit_tracker));
	TAILQ_INSERT_TAIL((kthread_runq->active_credit_tracker), u_new, uthread_creditq);
	
	gt_spin_unlock(runq_lock);

	fprintf(stderr, "Thread %d is put to kthread %d runq, kthread active q: (%d), kthread expired q: (%d)\n", 
	u_new->uthread_tid, u_new->cpu_id, kthread_runq->active_runq->uthread_tot , kthread_runq->expires_runq->uthread_tot);
	/* WARNING : DONOT USE u_new WITHOUT A LOCK, ONCE IT IS ENQUEUED. */

	/* Resume with the old thread (with all signals enabled) */
	// kthread_unblock_signal(SIGVTALRM);
	// kthread_unblock_signal(SIGUSR1);

	return 0;
}

#if 0
/**********************************************************************/
kthread_runqueue_t kthread_runqueue;
kthread_runqueue_t *kthread_runq = &kthread_runqueue;
sigjmp_buf kthread_env;

/* Main Test */
typedef struct uthread_arg
{
	int num1;
	int num2;
	int num3;
	int num4;	
} uthread_arg_t;

#define NUM_THREADS 10
static int func(void *arg);

int main()
{
	uthread_struct_t *uthread;
	uthread_t u_tid;
	uthread_arg_t *uarg;

	int inx;

	/* XXX: Put this lock in kthread_shared_info_t */
	gt_spinlock_init(&uthread_group_penalty_lock);

	/* spin locks are initialized internally */
	kthread_init_runqueue(kthread_runq);

	for(inx=0; inx<NUM_THREADS; inx++)
	{
		uarg = (uthread_arg_t *)MALLOC_SAFE(sizeof(uthread_arg_t));
		uarg->num1 = inx;
		uarg->num2 = 0x33;
		uarg->num3 = 0x55;
		uarg->num4 = 0x77;
		uthread_create(&u_tid, func, uarg, (inx % MAX_UTHREAD_GROUPS));
	}

	kthread_init_vtalrm_timeslice();
	kthread_install_sighandler(SIGVTALRM, kthread_sched_vtalrm_handler);
	if(sigsetjmp(kthread_env, 0) > 0)
	{
		/* XXX: (TODO) : uthread cleanup */
		exit(0);
	}

	uthread_schedule(&ksched_priority);
	return(0);
}

static int func(void *arg)
{
	unsigned int count;
#define u_info ((uthread_arg_t *)arg)
	fprintf(stderr, "Thread %d created\n", u_info->num1);
	count = 0;
	while(count <= 0xffffff)
	{
		if(!(count % 5000000))
			fprintf(stderr, "uthread(%d) => count : %d\n", u_info->num1, count);
		count++;
	}
#undef u_info
	return 0;
}
#endif