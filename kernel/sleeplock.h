#ifndef SLEEPLOCK_H
#define SLEEPLOCK_H

struct sleeplock {
  uint locked;
  struct spinlock lk;
  char *name;
  int pid;
};

#endif // SLEEPLOCK_H
