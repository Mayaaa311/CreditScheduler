# Project 1 - Thread scheduling with GTthreads

------

The goal of the project is to understand and implement the **credit scheduler** in a user-level threads library(provided GTThreads library).
The library implements a O(1) priority scheduler and a priority co-scheduler for reference.

# My credit scheduler design: 

## DATA STRUCTURE: 

Each kthread contains active and expired queue(TAILQ heads), storing positive credit uthreads in active and negative credit uthreads in expired. These two are separate variables than the original active and expired queue, but all queues are stored in runqueue objects. 
For each uthread, I added a field ‘credit’ for keeping track of its current credit and a ‘init_credit’ to keep track of its initialized/max credit. I also added a ‘start_time’ for timing and credit deduction

## CREDIT POLICY(credit_scheduler(), gt_utrhead.c): 

Each time a uthread start executing, I store the current time into start_time in utrhead. Then, when the scheduler or gt_yield is called, I check current time and compute elapsed time by subtracting start_time from current time. Then, I would update the credit (credit -= running_time in millisecond * 2).
I chose the penalty factor to be 2 to make sure each cycle deduces about 20 credits : 10(ms) * 2 (penalty factor) = 20 credits
If credit > 0, keep running, if credit < 0, put into the expired queue.

## ADD CREDIT POLICY(credit_find_best_uthread(), gt_pq.c): 

For each time scheduler is called, I would do a loop through all uthread in the expired queue and boost them by (initial_credit / 5) credits, if the uthread’s credit is above 0 after the boosting, it is moved to the active queue, with their credit set to initial credit. By proportionally boosting the credit, I can make sure that the higher initial credit threads bounce back to the active queue faster than the lower initial credit threads.

## PICKING NEXT UTHREAD POLICY(credit_find_best_uthread(), gt_pq.c): 

When picking the next uthread, if the local active and expired queue are both empty, and total number of threads > 8, do load balance first.

## LOAD BALANCE(load_balance(), gt_pq.c): 

I collect all active queues from all kthread into one temporary queue, then redistribute all uthreads round robin. 
If after load balance, the active queue is still empty, then switch active and expired queue, setting all expired queue uthreads’ credits to default. 
After switching queue/load balance, I pick the next utrhead to run from the active queue through simply picking the head, since it is guaranteed for it to be positive credit.

## GT_YIELD CASE AND POLICY: 

Uthread 40, 60, 80, 100, 120 will yield once during their execution, yield is a function that simply calls the scheduler, because in the scheduler, it will either go to active queue if its credit is positive, or go to expired queue if its credit is negative. Both achieve the purpose of yielding the current run.

## scheduler overview
![newly created uthread](https://github.com/user-attachments/assets/f4abc4c6-611f-4537-8d25-f2299e62db4b)

