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
#include <stdbool.h>
#define NUM_CREDITS_PER_MS 2.0
#define PRINTFROM_CPU 0
/**********************************************************************/
/* runqueue operations */
static void __add_to_runqueue(runqueue_t *runq, uthread_struct_t *u_elm);
static void __rem_from_runqueue(runqueue_t *runq, uthread_struct_t *u_elm);

void print_credit_in_pq(int cpuid){

		kthread_context_t *k_ctx = kthread_cpu_map[cpuid];
		uthread_struct_t *u_obj;


		uthread_head_t *uthread_head = (k_ctx->krunqueue.active_credit_tracker);
		fprintf(stderr, "active q: [");
		int ct = 0;
		TAILQ_FOREACH(u_obj, uthread_head, uthread_creditq) {
			   fprintf(stderr, "A%d(c=%d), ", u_obj->uthread_tid,u_obj->credit);
			   ct ++;
		}
		fprintf(stderr, "] \n");

		uthread_head = (k_ctx->krunqueue.expired_credit_tracker);
		int ct1 = 0;
		fprintf(stderr, "expired q: [");
		TAILQ_FOREACH(u_obj, uthread_head, uthread_creditq) {
			   fprintf(stderr, "E%d(c=%d), ", u_obj->uthread_tid,u_obj->credit);
            ct1++;
		}
		fprintf(stderr, "] \n");


}
/**********************************************************************/
/* runqueue operations */
static inline void  __add_to_runqueue(runqueue_t *runq, uthread_struct_t *u_elem)
{
	unsigned int uprio, ugroup;
	uthread_head_t *uhead;

	/* Find a position in the runq based on priority and group.
	 * Update the masks. */
	uprio = u_elem->uthread_priority;
	ugroup = u_elem->uthread_gid;

	/* Insert at the tail */
	uhead = &runq->prio_array[uprio].group[ugroup];
	TAILQ_INSERT_TAIL(uhead, u_elem, uthread_runq);

	/* Update information */
	if(!IS_BIT_SET(runq->prio_array[uprio].group_mask, ugroup))
		SET_BIT(runq->prio_array[uprio].group_mask, ugroup);

	runq->uthread_tot++;

	runq->uthread_prio_tot[uprio]++;
	if(!IS_BIT_SET(runq->uthread_mask, uprio))
		SET_BIT(runq->uthread_mask, uprio);

	runq->uthread_group_tot[ugroup]++;
	if(!IS_BIT_SET(runq->uthread_group_mask[ugroup], uprio))
		SET_BIT(runq->uthread_group_mask[ugroup], uprio);

	return;
}

static inline void __rem_from_runqueue(runqueue_t *runq, uthread_struct_t *u_elem)
{
	unsigned int uprio, ugroup;
	uthread_head_t *uhead;

	/* Find a position in the runq based on priority and group.
	 * Update the masks. */
	uprio = u_elem->uthread_priority;
	ugroup = u_elem->uthread_gid;

	/* Insert at the tail */
	uhead = &runq->prio_array[uprio].group[ugroup];
	TAILQ_REMOVE(uhead, u_elem, uthread_runq);

	/* Update information */
	if(TAILQ_EMPTY(uhead))
		RESET_BIT(runq->prio_array[uprio].group_mask, ugroup);

	runq->uthread_tot--;

	if(!(--(runq->uthread_prio_tot[uprio])))
		RESET_BIT(runq->uthread_mask, uprio);

	if(!(--(runq->uthread_group_tot[ugroup])))
	{
		fprintf(stderr, "Thread %d is put to kthread %d runq, with totoal of %d \n", 
	u_elem->uthread_tid, u_elem->cpu_id,  runq->uthread_group_tot[ugroup]);
		assert(TAILQ_EMPTY(uhead));
		RESET_BIT(runq->uthread_group_mask[ugroup], uprio);
	}

	return;
}


/**********************************************************************/
/* Exported runqueue operations */
extern void init_runqueue(runqueue_t *runq)
{
	uthread_head_t *uhead;
	int i, j;
	/* Everything else is global, so already initialized to 0(correct init value) */
	for(i=0; i<MAX_UTHREAD_PRIORITY; i++)
	{
		for(j=0; j<MAX_UTHREAD_GROUPS; j++)
		{
			uhead = &((runq)->prio_array[i].group[j]);
			TAILQ_INIT(uhead);
		}
	}
	return;
}

extern void add_to_runqueue(runqueue_t *runq, gt_spinlock_t *runq_lock, uthread_struct_t *u_elem)
{
	gt_spin_lock(runq_lock);
	runq_lock->holder = 0x02;
	__add_to_runqueue(runq, u_elem);
	gt_spin_unlock(runq_lock);
	return;
}

extern void rem_from_runqueue(runqueue_t *runq, gt_spinlock_t *runq_lock, uthread_struct_t *u_elem)
{
	gt_spin_lock(runq_lock);
	runq_lock->holder = 0x03;
	__rem_from_runqueue(runq, u_elem);
	gt_spin_unlock(runq_lock);
	return;
}


extern void switch_runqueue(runqueue_t *from_runq, gt_spinlock_t *from_runqlock, 
		runqueue_t *to_runq, gt_spinlock_t *to_runqlock, uthread_struct_t *u_elem)
{
	rem_from_runqueue(from_runq, from_runqlock, u_elem);
	add_to_runqueue(to_runq, to_runqlock, u_elem);
	return;
}


/**********************************************************************/

extern void kthread_init_runqueue(kthread_runqueue_t *kthread_runq)
{
	kthread_runq->active_runq = &(kthread_runq->runqueues[0]);
	kthread_runq->expires_runq = &(kthread_runq->runqueues[1]);
	kthread_runq->active_credit_tracker = &(kthread_runq->credit_queues[0]);
	kthread_runq->expired_credit_tracker = &(kthread_runq->credit_queues[1]);
	gt_spinlock_init(&(kthread_runq->kthread_runqlock));
	init_runqueue(kthread_runq->active_runq);
	init_runqueue(kthread_runq->expires_runq);
	kthread_runq->num_in_active = 0;
	kthread_runq->tot = 0;
	TAILQ_INIT(&(kthread_runq->zombie_uthreads));
	TAILQ_INIT((kthread_runq->active_credit_tracker));
	TAILQ_INIT((kthread_runq->expired_credit_tracker));
	return;
}

static void print_runq_stats(runqueue_t *runq, int runq_str)
{
	int inx;
	fprintf(stderr,"******************************************************\n");
	fprintf(stderr,"Run queue(%d) state : \n", runq_str);
	fprintf(stderr,"******************************************************\n");
	fprintf(stderr,"uthreads details - (tot:%d , mask:%x)\n", runq->uthread_tot, runq->uthread_mask);
	fprintf(stderr,"******************************************************\n");
	fprintf(stderr,"uthread priority details : \n");
	for(inx=0; inx<MAX_UTHREAD_PRIORITY; inx++)
		fprintf(stderr,"uthread priority (%d) - (tot:%d)\n", inx, runq->uthread_prio_tot[inx]);
	fprintf(stderr,"******************************************************\n");
	fprintf(stderr,"uthread group details : \n");
	for(inx=0; inx<MAX_UTHREAD_GROUPS; inx++)
		fprintf(stderr,"uthread group (%d) - (tot:%d , mask:%x)\n", inx, runq->uthread_group_tot[inx], runq->uthread_group_mask[inx]);
	fprintf(stderr,"******************************************************\n");
	return;
}

extern uthread_struct_t *sched_find_best_uthread(kthread_runqueue_t *kthread_runq)
{
	/* [1] Tries to find the highest priority RUNNABLE uthread in active-runq.
	 * [2] Found - Jump to [FOUND]
	 * [3] Switches runqueues (active/expires)
	 * [4] Repeat [1] through [2]
	 * [NOT FOUND] Return NULL(no more jobs)
	 * [FOUND] Remove uthread from pq and return it. */

	runqueue_t *runq;
	prio_struct_t *prioq;
	uthread_head_t *u_head;
	uthread_struct_t *u_obj;
	unsigned int uprio, ugroup;

	gt_spin_lock(&(kthread_runq->kthread_runqlock));
	kthread_runq->kthread_runqlock.holder = 0x04;
	runq = kthread_runq->active_runq;

	if(!(runq->uthread_mask))
	{ /* No jobs in active. switch runqueue */
		assert(!runq->uthread_tot);
		kthread_runq->active_runq = kthread_runq->expires_runq;
		kthread_runq->expires_runq = runq;

		runq = kthread_runq->expires_runq;
		gt_spin_unlock(&(kthread_runq->kthread_runqlock));
		// check if switched queue is empty, if empty, return null
		if(!runq->uthread_mask)
		{
			assert(!runq->uthread_tot);
			return NULL;
		}
	}
	gt_spin_lock(&(kthread_runq->kthread_runqlock));
	kthread_runq->kthread_runqlock.holder = 0x04;
	/* Find the highest priority bucket */
	uprio = LOWEST_BIT_SET(runq->uthread_mask);
	prioq = &(runq->prio_array[uprio]);

	assert(prioq->group_mask);
	ugroup = LOWEST_BIT_SET(prioq->group_mask);

	u_head = &(prioq->group[ugroup]);
	u_obj = TAILQ_FIRST(u_head);
	__rem_from_runqueue(runq, u_obj);

	gt_spin_unlock(&(kthread_runq->kthread_runqlock));
#if 0
	fprintf(stderr,"cpu(%d) : sched best uthread(id:%d, group:%d)\n", u_obj->cpu_id, u_obj->uthread_tid, u_obj->uthread_gid);
#endif
	return(u_obj);
}

void Load_balance(){
		kthread_context_t *k_ctx;
		uthread_head_t *u_head;
		uthread_struct_t *u_obj;
			// if load balance
		k_ctx =  kthread_cpu_map[kthread_apic_id()];

		if(k_ctx->lb_flag){
			// 1. go through all uthread to extract all active queue
			// 2. redistribute evenly to every k thread
			// REMEBER!! changet to (int)sysconf(_SC_NPROCESSORS_CONF);
			ksched_shared_info_t *ksched_info = &ksched_shared_info;	

			if(ksched_info->cur_lb == 1){
				return;
			}

			// #endif
			int num_cpus = 4;
			// move everything to lb_q
			int ctr1 = 0;
			for(int i = 0; i < num_cpus; i++){
				k_ctx = kthread_cpu_map[i];
				ctr1+=k_ctx->krunqueue.num_in_active;
			}
			if(ctr1 < 4)
				return;
			gt_spin_lock(&ksched_info->ksched_lock);
			ksched_info->cur_lb == 1;
			fprintf(stderr, "\nkthread %d BEFORE LOAD BALANCE___________________\n",k_ctx->cpuid);
			print_credit_in_pq(k_ctx->cpuid);
			// #ifdef DEBUG
			fprintf(stderr,"before load balance, kthread %d contains %d active\n", 
			kthread_cpu_map[0]->cpuid, kthread_cpu_map[0]->krunqueue.num_in_active);

			fprintf(stderr,"before load balance, kthread %d contains %d active\n",
			 kthread_cpu_map[1]->cpuid, kthread_cpu_map[1]->krunqueue.num_in_active);

			fprintf(stderr,"before load balance, kthread %d contains %d active\n", 
			kthread_cpu_map[2]->cpuid, kthread_cpu_map[2]->krunqueue.num_in_active);

			fprintf(stderr,"before load balance, kthread %d contains %d active\n",
			 kthread_cpu_map[3]->cpuid, kthread_cpu_map[3]->krunqueue.num_in_active);

			uthread_head_t lb_q;
			TAILQ_INIT(&lb_q);
			uthread_struct_t *next;
			for(int i = 0; i < num_cpus; i++){
				k_ctx = kthread_cpu_map[i];
				u_head = k_ctx->krunqueue.active_credit_tracker;

				for (u_obj = TAILQ_FIRST(u_head); u_obj != NULL; u_obj = next) {
					next = TAILQ_NEXT(u_obj, uthread_creditq); // Store next element before removal
					TAILQ_INSERT_TAIL(&lb_q, u_obj, uthread_lbq);
					TAILQ_REMOVE(u_head, u_obj, uthread_creditq);
					ctr1++;
					k_ctx->krunqueue.num_in_active--;
					k_ctx->krunqueue.tot--;
				}
			}
			int ctr = 0;
			// redistribute everything to each active credit tracker
			for (u_obj = TAILQ_FIRST(&lb_q); u_obj != NULL; u_obj = next) {
				next = TAILQ_NEXT(u_obj, uthread_lbq); 

				TAILQ_INSERT_TAIL(kthread_cpu_map[ctr%num_cpus]->krunqueue.active_credit_tracker, u_obj, uthread_creditq);
				TAILQ_REMOVE(&lb_q, u_obj, uthread_lbq);
				kthread_cpu_map[ctr%num_cpus]->krunqueue.num_in_active++;
				kthread_cpu_map[ctr%num_cpus]->krunqueue.tot++;
				ctr++;
			}

			ksched_info->cur_lb = 0;
			k_ctx =  kthread_cpu_map[kthread_apic_id()];
			// #ifdef DEBUG
			fprintf(stderr, "\nkthread %d AFTER LOAD BALANCE___________________\n",k_ctx->cpuid);
			print_credit_in_pq(k_ctx->cpuid);
			fprintf(stderr,"after load balance, kthread %d contains %d active \n",
			kthread_cpu_map[0]->cpuid, kthread_cpu_map[0]->krunqueue.num_in_active);

			fprintf(stderr,"after load balance, kthread %d contains %d active\n",
			kthread_cpu_map[1]->cpuid, kthread_cpu_map[1]->krunqueue.num_in_active);

			fprintf(stderr,"after load balance, kthread %d contains %d active\n", 
			kthread_cpu_map[2]->cpuid, kthread_cpu_map[2]->krunqueue.num_in_active);

			fprintf(stderr,"after load balance, kthread %d contains %d active\n",
			kthread_cpu_map[3]->cpuid, kthread_cpu_map[3]->krunqueue.num_in_active);

			gt_spin_unlock(&ksched_info->ksched_lock);
		}
		return;
}
extern uthread_struct_t *credit_find_best_uthread(kthread_runqueue_t *kthread_runq)
{
	runqueue_t *runq;
	runqueue_t *expiresq;
	prio_struct_t *prioq;
	uthread_head_t *u_head;
	uthread_struct_t *u_obj;
	uthread_struct_t *result;
	kthread_context_t *k_ctx;
	bool found_active= kthread_runq->num_in_active != 0;
	
	gt_spin_lock(&(kthread_runq->kthread_runqlock));
	runq = kthread_runq->active_runq;
	kthread_runq->kthread_runqlock.holder = 0x04;

	if(TAILQ_EMPTY(kthread_runq->active_credit_tracker))
	{ /* No jobs in active. switch runqueue */
		
		// if load balance
		k_ctx =  kthread_cpu_map[kthread_apic_id()];
		if(ksched_shared_info.kthread_tot_uthreads == 0){
			gt_spin_unlock(&(kthread_runq->kthread_runqlock));
			return NULL;
		}

		if(TAILQ_EMPTY(kthread_runq->expired_credit_tracker) && ksched_shared_info.kthread_cur_uthreads > 8){

			Load_balance();
		}

		u_head = (kthread_runq->expired_credit_tracker);
		if(TAILQ_EMPTY(kthread_runq->active_credit_tracker)){
			int ctr = 0;
			TAILQ_FOREACH(u_obj, u_head, uthread_creditq) {
				u_obj->credit = u_obj->init_credit; // Assuming `priority` is a field in uthread_struct_t
				ctr++;
			}

			uthread_head_t* temp = kthread_runq->active_credit_tracker;
			kthread_runq->active_credit_tracker = kthread_runq->expired_credit_tracker;
			kthread_runq->expired_credit_tracker = temp;
			kthread_runq->num_in_active = ctr;

		}
		
		if(TAILQ_EMPTY(kthread_runq->expired_credit_tracker) && TAILQ_EMPTY(kthread_runq->active_credit_tracker)){
			assert(!runq->uthread_tot);
			gt_spin_unlock(&(kthread_runq->kthread_runqlock));
			return NULL;
		}
		
	}
	else{
		u_head = kthread_runq->expired_credit_tracker;
		// if here are jobs in expired credit tracker, move all positive credit jobs to active credit tracker.
		uthread_struct_t *next;
		if(!TAILQ_EMPTY(u_head)){

			// replenish credit
			for (u_obj = TAILQ_FIRST(u_head); u_obj != NULL; u_obj = next) {
				next = TAILQ_NEXT(u_obj, uthread_creditq); // Store next element before removal

				int to_add = u_obj->init_credit / 5;
				if (u_obj->credit + to_add > u_obj->init_credit) {
					u_obj->credit = u_obj->init_credit; // Reset credit to max credit if it exceeds max
				} else {
					u_obj->credit += to_add;
				}

				if (u_obj->credit > 0) {
					// Move u_obj to active queue from expired queue
					TAILQ_REMOVE(kthread_runq->expired_credit_tracker, u_obj, uthread_creditq);
					TAILQ_INSERT_TAIL(kthread_runq->active_credit_tracker, u_obj, uthread_creditq);
					kthread_runq->num_in_active++;
				}
			}
			// if(kthread_apic_id()  == PRINTFROM_CPU){
			// 	fprintf(stderr, "hafter checkingand moving from epired to active runq\n");
			// 	print_credit_in_pq();
			// 	fprintf(stderr, "hafter checkingand moving from epired to active runq------------\n");
			// }
		}
	
	}
	// find out uthread with highest credit
	int most_credit = 0;


	u_head = (kthread_runq->active_credit_tracker);
	// if result is not NULL, remove it from credit tracker and runqueue
	// u_head = (kthread_runq->active_credit_tracker);
	result = TAILQ_FIRST(u_head);
	if(result){
		TAILQ_REMOVE(u_head, result, uthread_creditq);
		kthread_runq->num_in_active--;
		// __rem_from_runqueue(runq, result);
	}
	gt_spin_unlock(&(kthread_runq->kthread_runqlock));
	// if(kthread_apic_id()  == PRINTFROM_CPU)
	// 	print_credit_in_pq();
	return(result);
}


/* XXX: More work to be done !!! */
extern gt_spinlock_t uthread_group_penalty_lock;
extern unsigned int uthread_group_penalty;

extern uthread_struct_t *sched_find_best_uthread_group(kthread_runqueue_t *kthread_runq)
{
	/* [1] Tries to find a RUNNABLE uthread in active-runq from u_gid.
	 * [2] Found - Jump to [FOUND]
	 * [3] Tries to find a thread from a group with least threads in runq (XXX: NOT DONE)
	 * - [Tries to find the highest priority RUNNABLE thread (XXX: DONE)]
	 * [4] Found - Jump to [FOUND]
	 * [5] Switches runqueues (active/expires)
	 * [6] Repeat [1] through [4]
	 * [NOT FOUND] Return NULL(no more jobs)
	 * [FOUND] Remove uthread from pq and return it. */
	runqueue_t *runq;
	prio_struct_t *prioq;
	uthread_head_t *u_head;
	uthread_struct_t *u_obj;
	unsigned int uprio, ugroup, mask;
	uthread_group_t u_gid;

#ifndef COSCHED
	return sched_find_best_uthread(kthread_runq);
#endif

	/* XXX: Read u_gid from global uthread-select-criterion */
	u_gid = 0;
	runq = kthread_runq->active_runq;

	if(!runq->uthread_mask)
	{ /* No jobs in active. switch runqueue */
		assert(!runq->uthread_tot);
		kthread_runq->active_runq = kthread_runq->expires_runq;
		kthread_runq->expires_runq = runq;

		runq = kthread_runq->expires_runq;
		if(!runq->uthread_mask)
		{
			assert(!runq->uthread_tot);
			return NULL;
		}
	}


	if(!(mask = runq->uthread_group_mask[u_gid]))
	{ /* No uthreads in the desired group */
		assert(!runq->uthread_group_tot[u_gid]);
		return (sched_find_best_uthread(kthread_runq));
	}

	/* Find the highest priority bucket for u_gid */
	uprio = LOWEST_BIT_SET(mask);

	/* Take out a uthread from the bucket. Return it. */
	u_head = &(runq->prio_array[uprio].group[u_gid]);
	u_obj = TAILQ_FIRST(u_head);
	rem_from_runqueue(runq, &(kthread_runq->kthread_runqlock), u_obj);

	return(u_obj);
}

#if 0
/*****************************************************************************************/
/* Main Test Function */

runqueue_t active_runqueue, expires_runqueue;

#define MAX_UTHREADS 1000
uthread_struct_t u_objs[MAX_UTHREADS];

static void fill_runq(runqueue_t *runq)
{
	uthread_struct_t *u_obj;
	int inx;
	/* create and insert */
	for(inx=0; inx<MAX_UTHREADS; inx++)
	{
		u_obj = &u_objs[inx];
		u_obj->uthread_tid = inx;
		u_obj->uthread_gid = (inx % MAX_UTHREAD_GROUPS);
		u_obj->uthread_priority = (inx % MAX_UTHREAD_PRIORITY);
		__add_to_runqueue(runq, u_obj);
		fprintf(stderr,"Uthread (id:%d , prio:%d) inserted\n", u_obj->uthread_tid, u_obj->uthread_priority);
	}

	return;
}

static void change_runq(runqueue_t *from_runq, runqueue_t *to_runq)
{
	uthread_struct_t *u_obj;
	int inx;
	/* Remove and delete */
	for(inx=0; inx<MAX_UTHREADS; inx++)
	{
		u_obj = &u_objs[inx];
		switch_runqueue(from_runq, to_runq, u_obj);
		fprintf(stderr,"Uthread (id:%d , prio:%d) moved\n", u_obj->uthread_tid, u_obj->uthread_priority);
	}

	return;
}


static void empty_runq(runqueue_t *runq)
{
	uthread_struct_t *u_obj;
	int inx;
	/* Remove and delete */
	for(inx=0; inx<MAX_UTHREADS; inx++)
	{
		u_obj = &u_objs[inx];
		__rem_from_runqueue(runq, u_obj);
		fprintf(stderr,"Uthread (id:%d , prio:%d) removed\n", u_obj->uthread_tid, u_obj->uthread_priority);
	}

	return;
}

int main()
{
	runqueue_t *active_runq, *expires_runq;
	uthread_struct_t *u_obj;
	int inx;

	active_runq = &active_runqueue;
	expires_runq = &expires_runqueue;

	init_runqueue(active_runq);
	init_runqueue(expires_runq);

	fill_runq(active_runq);
	print_runq_stats(active_runq, "ACTIVE");
	print_runq_stats(expires_runq, "EXPIRES");
	change_runq(active_runq, expires_runq);
	print_runq_stats(active_runq, "ACTIVE");
	print_runq_stats(expires_runq, "EXPIRES");
	empty_runq(expires_runq);
	print_runq_stats(active_runq, "ACTIVE");
	print_runq_stats(expires_runq, "EXPIRES");

	return 0;
}

#endif