typedef struct queue {
	int capacity;
	int front;
	int back;
	int currentSize;
	double data[10];
} Queue;

int enqueue(Queue*, double);
int dequeue(Queue*);
double sum(Queue*);
double movingAverage(Queue*);
