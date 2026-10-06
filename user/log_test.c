#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

// Multi-process logging over a shared buffer.
//
// The parent forks NCHILD children, then allocates the log buffer and
// maps it into every child with map_shared_pages(). Each child learns
// where the buffer is mapped in its own address space through a pipe.
//
// Buffer layout: a sequence of [32-bit header][message bytes] records,
// each header 4-byte aligned. The header holds the writer's index in
// the upper 16 bits and the message length in the lower 16 bits; a
// zero header marks free space. A child claims a record by atomically
// swapping a zero header for its own, and skips records claimed by
// others. Since records are claimed in order from the start of the
// buffer, the first zero header marks the end of the log.

#define BUFSZ  PGSIZE
#define NCHILD 8
#define MAXMSG 128

#define ENCODE_HEADER(idx, len) (((uint32)(idx) << 16) | ((len) & 0xFFFF))
#define DECODE_INDEX(hdr)       (((hdr) >> 16) & 0xFFFF)
#define DECODE_LEN(hdr)         ((hdr) & 0xFFFF)
#define ALIGN4(a)               (((a) + 3) & ~3)

static void
append(char *buf, int *n, const char *s)
{
  while(*s)
    buf[(*n)++] = *s++;
}

static void
append_int(char *buf, int *n, int x)
{
  char tmp[16];
  int i = 0;

  do {
    tmp[i++] = '0' + x % 10;
    x /= 10;
  } while(x > 0);
  while(i > 0)
    buf[(*n)++] = tmp[--i];
}

// Build message number k of child idx into buf and return its length.
// The trailing padding makes message lengths differ between children
// and between messages.
static int
build_msg(char *buf, int idx, int k)
{
  int n = 0;
  int pad = (idx * 5 + k * 3) % 17;

  append(buf, &n, "child ");
  append_int(buf, &n, idx);
  append(buf, &n, " says hello #");
  append_int(buf, &n, k);
  append(buf, &n, " ");
  for(int i = 0; i < pad; i++)
    buf[n++] = 'a' + idx;
  buf[n] = 0;
  return n;
}

// Write messages into the shared buffer [addr, addr+BUFSZ) until the
// next message does not fit.
static void
child_log(int idx, uint64 addr)
{
  uint64 end = addr + BUFSZ;
  char msg[MAXMSG];
  int k = 0;
  int len = build_msg(msg, idx, k);

  while(addr + 4 + len <= end){
    uint32 old = __sync_val_compare_and_swap((uint32 *)addr, 0, ENCODE_HEADER(idx, len));
    if(old == 0){
      // claimed this record; the message bytes follow the header.
      memcpy((void *)(addr + 4), msg, len);
      addr = ALIGN4(addr + 4 + len);
      len = build_msg(msg, idx, ++k);
      sleep(1); // fairness: let the other children claim records too
    } else {
      // claimed by someone else; skip over their record.
      addr = ALIGN4(addr + 4 + DECODE_LEN(old));
    }
  }
}

// Print every record in the buffer.
static void
parent_read(uint64 addr)
{
  uint64 end = addr + BUFSZ;
  char msg[MAXMSG];
  int count = 0;

  while(addr + 4 <= end){
    uint32 hdr = *(uint32 *)addr;
    if(hdr == 0)
      break; // end of log
    int idx = DECODE_INDEX(hdr);
    int len = DECODE_LEN(hdr);
    if(addr + 4 + len > end || len >= MAXMSG)
      break;
    memcpy(msg, (void *)(addr + 4), len);
    msg[len] = 0;
    printf("[child %d] %s\n", idx, msg);
    count++;
    addr = ALIGN4(addr + 4 + len);
  }
  printf("log_test: %d messages, %d of %d bytes used\n",
         count, (int)(addr - (end - BUFSZ)), BUFSZ);
}

int
main(int argc, char *argv[])
{
  int pids[NCHILD];
  int fds[NCHILD][2];
  int ppid = getpid();

  // Fork the children first, so the buffer allocated below is not
  // copied into them.
  for(int i = 0; i < NCHILD; i++){
    if(pipe(fds[i]) < 0){
      printf("log_test: pipe failed\n");
      exit(1);
    }
    pids[i] = fork();
    if(pids[i] < 0){
      printf("log_test: fork failed\n");
      exit(1);
    }
    if(pids[i] == 0){
      // child: wait for the address of the buffer in our address space.
      uint64 addr;
      close(fds[i][1]);
      if(read(fds[i][0], &addr, sizeof(addr)) != sizeof(addr) || addr == 0)
        exit(1);
      close(fds[i][0]);
      child_log(i, addr);
      exit(0);
    }
    close(fds[i][0]);
  }

  char *buf = malloc(BUFSZ);
  if(buf == 0){
    printf("log_test: malloc failed\n");
    exit(1);
  }
  memset(buf, 0, BUFSZ);

  for(int i = 0; i < NCHILD; i++){
    uint64 addr = map_shared_pages(ppid, pids[i], buf, BUFSZ);
    if(addr == 0)
      printf("log_test: map_shared_pages failed for child %d\n", i);
    write(fds[i][1], &addr, sizeof(addr));
    close(fds[i][1]);
  }

  // Read only after every child is done, so no message is still being
  // written while we print it. The parent owns the buffer, so it must
  // also outlive the children's mappings of it.
  for(int i = 0; i < NCHILD; i++)
    wait(0);

  parent_read((uint64)buf);
  exit(0);
}
