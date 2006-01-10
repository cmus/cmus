#ifndef QUEUE_H
#define QUEUE_H

typedef struct {
	char *data;
	char *head;
	char *tail;
	int dataLen;		/* max size of queue */
	int bytesUsed;
} WRQUEUE, *PWRQUEUE, *LPWRQUEUE;

int wrqInitQueue(LPWRQUEUE q, int max);
void wrqDeinitQueue(LPWRQUEUE q);
int wrqFreeSpace(LPWRQUEUE q);
int wrqEnqueue(LPWRQUEUE q, char *buf, int numBytes);
int wrqDequeue(LPWRQUEUE q, char **buf, int max);
int wrqNumUsed(LPWRQUEUE q);
void wrqFlush(LPWRQUEUE q);

#endif
