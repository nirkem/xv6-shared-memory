#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

#define BUFSZ PGSIZE
#define MSG   "Hello daddy"

void print_sz(const char *label) {
  printf("[%s] sz: %p\n", label, sbrk(0));
}

int
main(int argc, char *argv[])
{
  int ppid = getpid();
  int disable_unmap = 0;
  
  if (argc > 1 && strcmp(argv[1], "--no-unmap") == 0)
    disable_unmap = 1;

  printf("\n===== Shared Memory Test Start =====\n");

  // Allocate memory in parent
  char *shared_buf = malloc(BUFSZ);
  if (!shared_buf) {
    printf("Parent malloc failed\n");
    exit(1);
  }
  printf("[PARENT] shared_buf: %p\n", shared_buf);
  // Write test data to buffer
  strcpy(shared_buf, "Hello Son");

  print_sz("PARENT before fork");

  int pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    // -------- CHILD PROCESS --------
    print_sz("CHILD before mapping");
    printf("[CHILD] shared_buf: %p\n", shared_buf);

    // Get shared mapping from parent into this child
    uint64 mapped_va = map_shared_pages(ppid, getpid(), shared_buf, BUFSZ);
    if (mapped_va == 0) {
      printf("[CHILD] map_shared_pages failed\n");
      exit(1);
    }

    char *shptr = (char *)mapped_va;

    printf("[CHILD] Read from shared: \"%s\"\n", shptr);

    // Write to shared memory
    strcpy(shptr, MSG);
    print_sz("CHILD after mapping");

    if (!disable_unmap) {
      if (unmap_shared_pages(shptr, BUFSZ) < 0) {
        printf("[CHILD] unmap_shared_pages failed\n");
      } else {
        print_sz("CHILD after unmap");
      }

      void *new_ptr = malloc(20*BUFSZ);
      if (new_ptr) {
        printf("[CHILD] malloc after unmap ok\n");
        print_sz("CHILD after malloc");
      } else {
        printf("[CHILD] malloc after unmap failed\n");
      }
    } else {
      printf("[CHILD] Skipping unmap (test orphaned shared pages)\n");
    }

    exit(0);
  }

  // -------- PARENT PROCESS --------
  wait(0); // Wait for child

  printf("[PARENT] Read after child: \"%s\"\n", shared_buf);
  print_sz("PARENT after wait");

  printf("===== Shared Memory Test Done =====\n\n");
  exit(0);
}
