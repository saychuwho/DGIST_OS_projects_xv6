#include "types.h"
#include "stat.h"
#include "user.h"

#define C_NUM		50 /* # of children */
//#define CMP_TIME	16 /* Completion time of child */
#define LONG_CMP_TIME 200

#define PRINT

#define MULTIPLE 10

int main(void)
{
	int p_begin, p_end; /* Parent time */
	int i;
	int pid[C_NUM];
	int cmp_time; /* Completion time of child */
	int f_begin, work, a;
	work = 0;

	printf(1, "# of task: %d, Completion time of each task: one [%d] and other [0,3,6,9,12,0,..] \n", C_NUM, LONG_CMP_TIME);

	p_begin = uptime();
	for (i=0; i<C_NUM; i++) {
		f_begin = uptime(); /* Task Arrival */
		pid[i] = fork();		
		if (pid[i] == 0) {
			//printf(1, "child [%d] begin: %d\n", i, uptime());
			if (i == 0) {
				// prj 01 extra
				nicevaluedown();

				cmp_time = LONG_CMP_TIME;

				while (work < cmp_time) {
					a = uptime();
					while (uptime() == a) {;}
					work++;
				}
				printf(1, "turnaround time of long task: %d\n", uptime() - f_begin);
			}
			else {
				cmp_time = (i%5) * (MULTIPLE);
				while (work < cmp_time) {
					a = uptime();
					while (uptime() == a) {;}
					work++;
				}
#ifdef PRINT
				printf(1, "turnaround time of short task [%d]: %d\n", i, uptime() - f_begin);
#endif
			}
			exit();
		}
	}
	for (i=0; i<C_NUM; i++)
		wait();
	p_end = uptime();
	printf(1, "Time from fork() to wait(): %d\n", p_end-p_begin);

	exit();
}
