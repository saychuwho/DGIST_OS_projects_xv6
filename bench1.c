#include "types.h"
#include "stat.h"
#include "user.h"

#define C_NUM		10 /* # of children */
#define CMP_TIME	0  /* Completion time of child */

#define PRINT

int main(void)
{
	int p_begin, p_end; /* Parent time */
	int i;
	int pid[C_NUM];
	int cmp_time; /* Completion time of child */
	int c_begin, c_end; /* Child time */
	int f_begin, work, a;
	int rsp, tr;
	int buf;
	int c_rsp_time = 0;
	int c_tr_time = 0;
	work = 0;

	int fds[C_NUM][2];
	for (i=0; i<C_NUM; i++) {
		if (pipe(fds[i]) == -1) {
			printf(1,"pipe err in pipe %d. check NOFILE in param.h\n", i);
			exit();
		}
	}
	printf(1, "# of task: %d, Completion time of each task: %d (tick) \n", C_NUM, CMP_TIME);

	p_begin = uptime();
	for (i=0; i<C_NUM; i++) {
		f_begin = uptime();
		pid[i] = fork();

		if (pid[i] == 0) {
			close(fds[i][0]);
			c_begin = uptime();
#ifdef PRINT
			printf(1, "child [%d] begin: %d\n", i, c_begin);
#endif
			cmp_time = CMP_TIME;
			/* Job */
			while (work < cmp_time) {
				a = uptime();
				while (uptime() == a) {;}
				work++;
			}
			c_end = uptime();
#ifdef PRINT
			printf(1, "child [%d] end: %d\n", i, c_end);
#endif
			rsp = c_begin - f_begin;
			tr = c_end - f_begin;
			buf = (rsp << 16) | tr;
			write(fds[i][1], &buf, sizeof(int));
			close(fds[i][1]);

			exit(); /* Task (child) exit */
		}
	}
	for (i=0; i<C_NUM; i++)
		wait();
	p_end = uptime();
	printf(1, "Time from fork() to wait(): %d\n", p_end-p_begin);


	for (i=0; i<C_NUM; i++) {
		read(fds[i][0], &buf, sizeof(buf));
		close(fds[i][0]);
#ifdef PRINT
		printf(1, "(%d) response time: %d, turaround time: %d\n", i, (buf&0xffff0000)>>16, buf&0x0000ffff);
#endif
		c_rsp_time += (buf&0xffff0000)>>16;
		c_tr_time += buf&0x0000ffff;
	}
	printf(1, "(Child) Total response time: %d (avg: %d), Total turnaround time: %d (avg: %d)\n", c_rsp_time, c_rsp_time/C_NUM, c_tr_time, c_tr_time/C_NUM);

	exit();
}
