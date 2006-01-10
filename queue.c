#include <xmalloc.h>
#include "queue.h"

int wrqInitQueue(LPWRQUEUE q, int max)
{
	if (!q)
		return 0;

	memset(q, 0, sizeof(*q));

	if (max <= 0)
		return 0;

	q->data = (char *)malloc(max);
	if (!q->data)
		return 0;

	q->head = q->tail = q->data;
	q->dataLen = max;

	memset(q->data, ' ', max);

	return -1;
}

void wrqDeinitQueue(LPWRQUEUE q)
{
	if (!q)
		return;

	if (q->data && q->dataLen)
		free(q->data);

	memset(q, 0, sizeof(*q));
}

int wrqFreeSpace(LPWRQUEUE q)
{
	int retVal;

	if (!q)
		return 0;
	retVal = q->dataLen - q->bytesUsed;

	return retVal;
}

int wrqNumUsed(LPWRQUEUE q)
{
	int retVal;

	if (!q)
		return 0;

	retVal = q->bytesUsed;

	return retVal;
}

void wrqFlush(LPWRQUEUE q)
{
	q->head = q->tail = q->data;
	q->bytesUsed = 0;
}

/*
 * returns the number of bytes enqueued
 */
int wrqEnqueue(LPWRQUEUE q, char *buf, int numBytes)
{
	char *end;
	int retVal = 0;

	if (!q || !buf)
		return 0;

	end = q->data + q->dataLen;

	if (numBytes > (q->dataLen - q->bytesUsed))
		numBytes = q->dataLen - q->bytesUsed;

	q->bytesUsed += numBytes;
	retVal = numBytes;

	/* if it all fits without wrap-around, just copy it */
	if (q->tail + numBytes < end) {
		memcpy(q->tail, buf, numBytes);
		q->tail += numBytes;
	} else {
		memcpy(q->tail, buf, end - q->tail);
		buf += (end - q->tail);
		numBytes -= (end - q->tail);
		q->tail = q->data;
		memcpy(q->tail, buf, numBytes);
		q->tail += numBytes;
	}

	return retVal;
}

int wrqDequeue(LPWRQUEUE q, char **buf, int max)
{
	char *end;
	int retVal = 0;

	if (!q || !buf || !q->bytesUsed || !*buf)
		return 0;

	end = q->data + q->dataLen;

	if (max > q->bytesUsed)
		max = q->bytesUsed;
	retVal = max;
	q->bytesUsed -= max;

	if (q->head + max < end) {
#if 1
		memcpy(*buf, q->head, max);
#else
		// return a pointer to the memory instead of copying it,
		// to try and speed up.  Since 1 min of audio represents
		// 10 MB of data, this could have a rather large effect
		*buf = q->head;
#endif
		q->head += max;
	} else {
		char *p;

		p = *buf;

		memcpy(p, q->head, end - q->head);
		p += (end - q->head);
		max -= (end - q->head);
		q->head = q->data;
		memcpy(p, q->head, max);
		q->head += max;
	}

	if (q->bytesUsed == 0)
		q->head = q->tail = q->data;

	return retVal;
}
