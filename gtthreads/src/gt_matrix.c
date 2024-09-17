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
	int size;
	int **m;
	int rows;
	int cols;
	unsigned int reserved[2];
} matrix_t;


typedef struct __uthread_arg
{
	matrix_t *_A, *_B, *_C;
	unsigned int reserved0;
	int yield;
	unsigned int tid;
	unsigned int gid;
	unsigned int size;
	int credit;

}uthread_arg_t;

struct timeval tv1;

static void generate_matrix(matrix_t *mat, int size, int val) {
    mat->rows = size;
    mat->cols = size;
	mat->size = size;
    mat->m = (int **)malloc(size * sizeof(int *));
    for (int i = 0; i < size; i++) {
        mat->m[i] = (int *)malloc(size * sizeof(int));
        for (int j = 0; j < size; j++) {
            mat->m[i][j] = val;
        }
    }
}

static void print_matrix(matrix_t *mat)
{
	int i, j;
	printf("x %d", mat->size);
	for(i=0;i<mat->size;i++)
	{
		for(j=0;j<mat->size;j++)
			fprintf(stderr, " %d ",mat->m[i][j]);
		fprintf(stderr, "\n");
	}

	return;
}

extern int uthread_create(uthread_t *, void *, void *, uthread_group_t, int, int);
static void init_matrices(matrix_t *A, matrix_t *B, matrix_t *C, int size) {
    generate_matrix(A, size, 1);
    generate_matrix(B, size, 1);
    generate_matrix(C, size, 0);
}
static void * uthread_mulmat(void *p)
{
	
	int i, j, k;
	unsigned int cpuid;
	struct timeval tv2;

#define ptr ((uthread_arg_t *)p)
	// init_matrices(ptr->_A,ptr->_B,ptr->_C, ptr->size);
	i=0; j= 0; k=0;

	// cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
	// fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) started",ptr->tid, ptr->gid, cpuid);
	// printf("I GOT HERE!!!");
	// printf("matrix size: %d\n", ptr->size);
	
	for(i = 0; i < ptr->size; i++){
		if(i == 50 && ptr->yield == 1){
			fprintf(stderr, "uthred %d yied!\n",ptr->tid);
			gt_yield(ptr->tid);
		}
		for(j = 0; j < ptr->size; j++)
			for(k = 0; k < ptr->size; k++){
				// printf("MULPSHAUIHDUI");
				ptr->_C->m[i][j] += ptr->_A->m[i][k] * ptr->_B->m[k][j];
				// printf("abababab");
			}
	}
// printf("I GOT HEafdsfdRE!!!");
	gettimeofday(&tv2,NULL);
	fprintf(stderr, "\nThread(id:%d, credit:%d, size: %d) finished (TIME : %lu s and %lu us)",
			ptr->tid, ptr->credit, ptr->size, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
	// print_matrix(ptr->_C);

#undef ptr
	return 0;
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
	int credit;
	matrix_t matAs[4];
	matrix_t matBs[4];
	matrix_t matCs[4];
	int matrix_sz[4] = {32, 64, 128, 256};

	parse_args(argc, argv);

	gtthread_app_init(lb_flag, priority_schedule);

	for(int i = 0; i < 4; i++){
		init_matrices(&matAs[i],&matBs[i],&matCs[i], matrix_sz[i]);
	}

	int credits[4] =  {25, 50, 75, 100};
	// int credits[4] = {25,25,25,25};
	gettimeofday(&tv1,NULL);
	int credit_inx = 0;
	int uthread_tid = 0;
	for(inx=0; inx<4; inx++)
	{	
		for(int k = 0; k < 4; k++)
			for(int j=0; j<8; j++){
				uarg = &uargs[uthread_tid];
				uarg->_A = &matAs[3-inx];
				uarg->_B = &matBs[3-inx];
				uarg->_C = &matCs[3-inx];
				uarg->yield = 0;
				if( uthread_tid % 20 == 0){
					uarg->yield = 1;
				}

				uarg->tid = uthread_tid;
				#ifdef DEBUG
				fprintf(stderr,"mat size :%d\n", matrix_sz[inx]);
				#endif
				uarg->size = matrix_sz[3-inx];

				uarg->gid = 0;
				uarg->credit = credits[k];
				#ifdef DEBUG
				fprintf(stderr, "credit:%d, credit_idx: %d\n", credits[k], k);
				#endif
				// printf("Creating thread(id:%d, credit:%d, size:%d)...\n", uarg->tid, uarg->credit, uarg->size);
				uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, credits[k],matrix_sz[3-inx]);
				uthread_tid++;
			}
		// if(inx != 0 && inx % 4 == 0) credit_inx++;
	}

	gtthread_app_exit();
	// print_matrix(&A);
	// print_matrix(&B);

	return(0);
}

