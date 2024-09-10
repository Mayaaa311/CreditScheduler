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
#include <string.h>
#include <stdbool.h>
#include "gt_include.h"



#define SIZE 250
#define CREDIT 10

// #define NUM_CPUS 2
// #define NUM_GROUPS 20
// #define PER_GROUP_COLS (SIZE/NUM_GROUPS)

#define NUM_THREADS 128
// #define PER_THREAD_ROWS (SIZE/NUM_THREADS)

unsigned int lb_flag;
unsigned int priority_schedule;
/* A[SIZE][SIZE] X B[SIZE][SIZE] = C[SIZE][SIZE]
 * Let T(g, t) be thread 't' in group 'g'. 
 * T(g, t) is responsible for multiplication : 
 * A(rows)[(t-1)*SIZE -> (t*SIZE - 1)] X B(cols)[(g-1)*SIZE -> (g*SIZE - 1)] */

typedef struct matrix
{
	int m[SIZE][SIZE];

	int rows;
	int cols;
	unsigned int reserved[2];
} matrix_t;


typedef struct __uthread_arg
{
	matrix_t *_A, *_B, *_C;
	unsigned int reserved0;

	unsigned int tid;
	unsigned int gid;
	unsigned int size;
	int credit;

}uthread_arg_t;

struct timeval tv1;

static void generate_matrix(matrix_t *mat, int val)
{

	int i,j;
	mat->rows = SIZE;
	mat->cols = SIZE;
	for(i = 0; i < mat->rows;i++)
		for( j = 0; j < mat->cols; j++ )
		{
			mat->m[i][j] = val;
		}
	return;
}

static void print_matrix(matrix_t *mat)
{
	int i, j;

	for(i=0;i<SIZE;i++)
	{
		for(j=0;j<SIZE;j++)
			fprintf(stderr, " %d ",mat->m[i][j]);
		fprintf(stderr, "\n");
	}

	return;
}

extern int uthread_create(uthread_t *, void *, void *, uthread_group_t, int);

static void * uthread_mulmat(void *p)
{
	int i, j, k;
	unsigned int cpuid;
	struct timeval tv2;

#define ptr ((uthread_arg_t *)p)

	i=0; j= 0; k=0;

	// cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
	// fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) started",ptr->tid, ptr->gid, cpuid);


	for(i = 0; i < ptr->size; i++)
		for(j = 0; j < ptr->size; j++)
			for(k = 0; k < ptr->size; k++)
				ptr->_C->m[i][j] += ptr->_A->m[i][k] * ptr->_B->m[k][j];

	// gettimeofday(&tv2,NULL);
	// fprintf(stderr, "\nThread(id:%d, group:%d) finished (TIME : %lu s and %lu us)",
	// 		ptr->tid, ptr->gid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));

#undef ptr
	return 0;
}

matrix_t A, B, C;

static void init_matrices()
{
	generate_matrix(&A, 1);
	generate_matrix(&B, 1);
	generate_matrix(&C, 0);

	return;
}


void parse_args(int argc, char* argv[])
{
	int inx;

	for(inx=0; inx<argc; inx++)
	{
		if(argv[inx][0]=='-')
		{
			if(!strcmp(&argv[inx][1], "lb"))
			{
				//TODO: add option of load balancing mechanism
				printf("enable load balancing\n");
				lb_flag = true;
			}
			else if(!strcmp(&argv[inx][1], "s"))
			{
				//TODO: add different types of scheduler
				inx++;
				if(!strcmp(&argv[inx][0], "0"))
				{
					printf("use priority scheduler\n");
					priority_schedule = true;
				}
				else if(!strcmp(&argv[inx][0], "1"))
				{
					printf("use credit scheduler\n");
					priority_schedule = false;
				}
			}
		}
	}

	return;
}



uthread_arg_t uargs[NUM_THREADS];
uthread_t utids[NUM_THREADS];

int main(int argc, char *argv[])
{
	uthread_arg_t *uarg;
	int inx;

	

	parse_args(argc, argv);

	gtthread_app_init(lb_flag, priority_schedule);

	init_matrices();

	gettimeofday(&tv1,NULL);

	for(inx=0; inx<8; inx++)
	{
		uarg = &uargs[inx];
		uarg->_A = &A;
		uarg->_B = &B;
		uarg->_C = &C;

		uarg->tid = inx;
		uarg->size = SIZE;

		uarg->gid = 1;

		// printf("Creating thread(id:%d, credit:%d, size:%d)...\n", uarg->tid, uarg->credit, uarg->size);

		uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, CREDIT);
	}

	gtthread_app_exit();
	// print_matrix(&A);
	// print_matrix(&B);
	// print_matrix(&C);
	fprintf(stderr, "********************************");
	return(0);
}

