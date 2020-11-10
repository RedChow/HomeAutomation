#include "queue.h"

/*
 * We will be using this Queue struct to calculate a moving average from the distance sensor. This 
 * will help ensure that an object is within range by eliminating readings that may be from other
 * objects inadvertantly picked by the sensor. 
 * 
 * The program shouldn't have to worry about removing items from the Queue, as this should be
 * done automatically through the enqueue method.
 */

int enqueue(Queue *queue, double value) {
	//Return 0 if the Queue updates successfully, -1 otherwise
	int returnCode = 0;
	if (queue->capacity == queue->currentSize) {
		returnCode = dequeue(queue);
		//Famous last words: this probably won't get called as
		//dequeue will return -1 here if currentSize==capacity==0
		if (returnCode < 0) {
			return returnCode;
		}
	}
	queue->back = (queue->back + 1) % queue->capacity;
	queue->currentSize++;
	queue->data[queue->back] = value;
	return returnCode;
}

int dequeue(Queue *queue) {
	//Return 0 if data was removed successfully, -1 otherwise
	if (queue->currentSize == 0) {
		return -1;
	}
	queue->front = (queue->front + 1) % queue->capacity;
	queue->currentSize--;
	return 0;
}

double sum(Queue *queue) {
	double temp = 0.0;
	int i = 0;
	for (i = 0; i < queue->currentSize; i++) {
		temp += queue->data[i];
	}
	return temp;
}

double movingAverage(Queue *queue) {
	double s = sum(queue);
	if (queue->currentSize != 0) {
		return s/queue->currentSize;
	}
	return 0;
}

