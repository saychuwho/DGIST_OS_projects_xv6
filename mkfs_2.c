#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include "param.h"

#define BSIZE 512

int main(int argc, char *argv[])
{
	//unsigned char buf[BSIZE];
	//int ret;
	int rdfd = open(argv[1], O_RDONLY);
	if (rdfd < 0) {
		perror("rdfd open");
		exit(1);
	}
	int wrfd = open(argv[2], O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (wrfd < 0) {
		perror("wrfd open");
		exit(1);
	}

	//TODO implement here
	// RAID 5
	ftruncate(wrfd, (FSSIZE * 3) * BSIZE);

	unsigned char buf_1[BSIZE];
	unsigned char buf_2[BSIZE];
	unsigned char parity_block[BSIZE];
	int ret_1;
	int ret_2;
	int ret_3;
	for(int blockno = 0;blockno < FSSIZE; blockno++){
		// first, read A1 (0 ~ 999), A2 (1000 ~ 2000) from tmp.img
		ret_1 = pread(rdfd, buf_1, BSIZE, (BSIZE*blockno));
		//if (ret_1 == 0) // potential error occur in here..... if A2 is exist when after A1?
		//	break;
		if (ret_1 != BSIZE) {
			perror("pread");
			exit(1);
		}

		// buf_2에 해당하는 내용이 없다!
		/*
		ret_2 = pread(rdfd, buf_2, BSIZE, (BSIZE*(blockno + FSSIZE)));
		if(ret_2 == 0) // potential error occur in here..... if A2 is exist when after A1?
			break;
		if(ret_2 != BSIZE){
			perror("pread");
			exit(1);
		}
		*/

		// second, make parity block with buf_1, buf_2(해당하는 내용이 없으니 그냥 ^ 0을 한다)
		// buf_2에는 0을 집어넣자
		for(int i=0;i<BSIZE;i++){
			buf_2[i] = 0;
			parity_block[i] = buf_1[i] ^ buf_2[i];
		}

		// third, put parity block, raid block with modular calculation
		int parity_index = blockno % 3;
		switch(parity_index){
			case 0:
				// parity_block >> DISK 0
				ret_1 = pwrite(wrfd, parity_block, BSIZE, (BSIZE * blockno));
				if (ret_1 != BSIZE) {
					perror("pwrite1");
					exit(1);
				}
				// 0~999 >> DISK 1
				ret_2 = pwrite(wrfd, buf_1, BSIZE, (blockno + FSSIZE) * BSIZE);
				if (ret_2 != BSIZE) {
					perror("pwrite2");
					exit(1);
				}
				// 1000~1999 >> DISK 3
				ret_3 = pwrite(wrfd, buf_2, BSIZE, (blockno + FSSIZE*2) * BSIZE);
				if (ret_3 != BSIZE) {
					perror("pwrite3");
					exit(1);
				}
				break;
			case 1:
				// 0~999 >> DISK 0
				ret_1 = pwrite(wrfd, buf_1, BSIZE, (BSIZE * blockno));
				if (ret_1 != BSIZE) {
					perror("pwrite1");
					exit(1);
				}
				// parity_block >> DISK 1
				ret_2 = pwrite(wrfd, parity_block, BSIZE, (blockno + FSSIZE) * BSIZE);
				if (ret_2 != BSIZE) {
					perror("pwrite2");
					exit(1);
				}
				// 1000~1999 >> DISK 3
				ret_3 = pwrite(wrfd, buf_2, BSIZE, (blockno + FSSIZE*2) * BSIZE);
				if (ret_3 != BSIZE) {
					perror("pwrite3");
					exit(1);
				}
				break;
			case 2:
				// 0~999 >> DISK 0
				ret_1 = pwrite(wrfd, buf_1, BSIZE, (BSIZE * blockno));
				if (ret_1 != BSIZE) {
					perror("pwrite1");
					exit(1);
				}
				// 1000~1999 >> DISK 1
				ret_2 = pwrite(wrfd, buf_2, BSIZE, (blockno + FSSIZE) * BSIZE);
				if (ret_2 != BSIZE) {
					perror("pwrite2");
					exit(1);
				}
				// parity_block >> DISK 3
				ret_3 = pwrite(wrfd, parity_block, BSIZE, (blockno + FSSIZE*2) * BSIZE);
				if (ret_3 != BSIZE) {
					perror("pwrite3");
					exit(1);
				}
				break;
		}
	}

	/*
	// RAID 1
	ftruncate(wrfd, (FSSIZE * 2) * BSIZE);

	// RAID 1 : read block from tmp.img and write to fs.img
	for (int blockno = 0; blockno < FSSIZE; blockno++) {
		// tmp.img read
		ret = pread(rdfd, buf, BSIZE, (BSIZE * blockno));
		if (ret == 0)
			break;
		if (ret != BSIZE) {
			perror("pread");
			exit(1);
		}
		// tmp.img >> DISK 0
		ret = pwrite(wrfd, buf, BSIZE, (BSIZE * blockno));
		if (ret != BSIZE) {
			perror("pwrite1");
			exit(1);
		}
		// tmp.img >> DISK 1
		ret = pwrite(wrfd, buf, BSIZE, (blockno + FSSIZE) * BSIZE);
		if (ret != BSIZE) {
			perror("pwrite2");
			exit(1);
		}
	}
	*/


	close(rdfd);
	close(wrfd);
}
