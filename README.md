# xv6 Shared Memory & Lock-Free Multi-Process Logging

An extension of [xv6-riscv](https://github.com/mit-pdos/xv6-riscv), MIT's teaching Unix, that adds
**cross-process shared memory** to the kernel and builds a **lock-free multi-process logger** on top of it
in user space.

Built for the Operating Systems course (202.1.3031) at Ben-Gurion University of the Negev.

## What it does

### 1. Kernel: shared memory between processes

Two new system calls let one process map another process's physical pages into its own address space:

```c
uint64 map_shared_pages(int src_pid, int dst_pid, void *src_va, uint64 size);
int    unmap_shared_pages(void *addr, uint64 size);
```

- **Page-table level mapping.** `map_shared_pages` walks the source process's Sv39 page table, finds the
  physical page behind each virtual page, and installs it in the destination at the first free
  page-aligned address above its heap. It keeps the source's permission bits and returns the
  destination address with the same offset into the page as `src_va`.
- **Ownership tracking with a software PTE bit.** Bit 8 of the RISC-V PTE (`PTE_S`, unused by the
  hardware) marks a page as *borrowed*. `uvmunmap` clears the mapping but frees the physical page only
  when the process owns it, so when a process exits it never frees memory it doesn't own (no
  double frees).
- **Robust failure handling.** Bad pids, out-of-range addresses (≥ `MAXVA`), size overflow and partial
  failures are rejected cleanly. If mapping fails partway, the pages already mapped are removed
  again, so no stray entries are left in the page table.
- **Correct interaction with the rest of the kernel.** `fork` gives the child a private copy of a
  borrowed page (and clears `PTE_S` on it, so the copy isn't leaked). Unmapping a region that isn't
  at the top of the address space leaves a gap below `sz`; `fork` and `exit` skip such gaps instead
  of panicking.

Main changes: [`kernel/vm.c`](kernel/vm.c), [`kernel/sysproc.c`](kernel/sysproc.c),
[`kernel/proc.c`](kernel/proc.c), [`kernel/riscv.h`](kernel/riscv.h).

### 2. User space: lock-free logging over shared memory

[`user/log_test.c`](user/log_test.c) forks 8 writer processes that all log into a single shared 4 KB
buffer at the same time, **without locks**:

```
| hdr | message ... | hdr | message ... | hdr | ...          | 0 0 0 0 (free) |
  hdr = [ uint16 child index | uint16 length ]   (4-byte aligned)
```

- A writer claims the next record with an atomic **compare-and-swap** on its 32-bit header
  (`__sync_val_compare_and_swap`). Only one writer can turn a zero header into its own.
- If the CAS fails, the writer reads the winner's length from the returned value and jumps over that
  record. Since records are claimed in order from the start of the buffer, they stay contiguous and
  never overlap, whatever their lengths.
- The parent passes each child the real mapped address over a pipe (no guessing based on timing),
  waits for all writers to finish, then walks the records and prints them along with each writer's
  index.

[`user/shmem_test.c`](user/shmem_test.c) tests the shared-memory syscalls themselves: the child
writes `"Hello daddy"` into the parent's memory, and the test prints the address-space size at each
step to show that mapping, unmapping and `malloc` keep `sz` consistent. Run it with `--no-unmap` to
check that a child exiting while still mapped leaves the parent's memory intact.

## Running

Requires a RISC-V GCC toolchain and `qemu-system-riscv64` (e.g. on Ubuntu/WSL:
`sudo apt install gcc-riscv64-linux-gnu qemu-system-misc`), or use the included devcontainer.

```bash
make qemu
```

Then, at the xv6 shell:

```
$ shmem_test
$ shmem_test --no-unmap
$ log_test
$ usertests -q      # the full xv6 regression suite still passes
```

Sample `log_test` output (messages differ in length on purpose, to show variable-length records):

```
[child 0] child 0 says hello #0
[child 1] child 1 says hello #0 bbbbb
[child 2] child 2 says hello #0 cccccccccc
[child 3] child 3 says hello #0 ddddddddddddddd
...
log_test: 114 messages, 4072 of 4096 bytes used
```

## Credits

The xv6 base is © Frans Kaashoek, Robert Morris and Russ Cox (MIT license, see [`LICENSE`](LICENSE)
and [`README`](README)). The shared-memory and logging extensions were written for this course
assignment.
