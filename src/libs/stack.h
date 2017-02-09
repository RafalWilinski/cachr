#ifndef _STACK_H
#define _STACK_H

typedef char stackElementT;

typedef struct {
  stackElementT *contents;
  int maxSize;
  int top;
} stackT;

void StackInit(stackT *stackP, int maxSize);

void StackDestroy(stackT *stackP);

void StackPush(stackT *stackP, stackElementT element);

stackElementT StackPop(stackT *stackP);

int StackIsEmpty(stackT *stackP);
int StackIsFull(stackT *stackP);

#endif  /* not defined _STACK_H */
