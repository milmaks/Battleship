#define _CRT_SECURE_NO_WARNINGS

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ws2tcpip.h>
#include "Queue.h"

CRITICAL_SECTION cs;

void Enqueue(char* message, element **root) {
	EnterCriticalSection(&cs);

	element* temp = *root;

	element* newEl = (element*)malloc(sizeof(element));
	newEl->next = temp;
	strcpy(newEl->value, message);

	*root = newEl;

	LeaveCriticalSection(&cs);
}

char* Dequeue(element **root) {
	EnterCriticalSection(&cs);

	if (*root == NULL) {
		LeaveCriticalSection(&cs);
		return NULL;
	}


	int count = QueueCount(*root);
	element* temp = *root;

	for (int i = 0; i < count - 2; i++)
	{
		temp = temp->next;
	}

	char message[1024];

	if (count != 1) {
		element* del = temp->next;
		strcpy(message, del->value);
		free(del);
		del = NULL;
		temp->next = NULL;
	}
	else {
		strcpy(message, temp->value);
		free(temp);
		temp = NULL;
		*root = NULL;
	}

	LeaveCriticalSection(&cs);

	return message;
}

void InitQueue(element** root) {
	InitializeCriticalSection(&cs);
	*root = NULL;
}

int QueueCount(element* root) {
	int count = 0;
	element* temp = root;

	while (temp != NULL) {
		count++;
		temp = temp->next;
	}

	return count;
}