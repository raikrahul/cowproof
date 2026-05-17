# cowproof

When you call `fork()`, does your data get copied? Every OS textbook says "copy on write." None of them prove it. This repo does — with kernel modules, ring 0 page table walks, and live DRAM addresses from a real machine.

Kernel: `7.0.0-070000rc4-generic` (x86_64). All code compiles and runs. Logs are real.

---

## 1

You have a number in memory.

```c
int num = 100;
```

You call `fork()`. Now two processes exist. The CPU put `56` (clone) into `rax`, executed `syscall`, switched to ring 0. The kernel made a new `task_struct`. It returned the child's pid to the parent in `rax`, and `0` to the child in `rax`. Both resume at the same instruction.

The question: when both processes read `num`, are they reading the same bytes from the same DRAM chip? Or did the kernel copy 4096 bytes of memory?

## 2

We wrote a program. Parent reads `num`. Child reads `num`. Both print 100.

```c
int num = 100;
pid_t pid = fork();
if (pid == 0) {
    printf("child before write: %d\n", num);
    num = 42;
    printf("child after write: %d\n", num);
} else if (pid > 0) {
    wait(NULL);
    printf("parent value: %d\n", num);
}
```

Output:
```
child before write: 100
child after write: 42
parent value: 100
```

Child changed `num` to 42. Parent still sees 100. They're not sharing anymore. When did the split happen?

## 3

We need to see the physical address. Not the virtual one you get from `&num` — the actual DRAM row.

The CPU stores a 64-bit integer for every 4096-byte page. This integer lives in a table in DRAM. Bits 12-51 of that integer hold the physical frame number. Bit 1 says whether writes are allowed. Bit 0 says whether the page exists in DRAM at all.

```
64-bit entry in the page table:

bit 0       1 = exists in DRAM
bit 1       1 = writes allowed, 0 = trap on write
bits 12-51  physical frame number (which 4096-byte row of DRAM)
```

The file `/proc/self/pagemap` exposes this. You divide a virtual address by 4096, multiply by 8, seek to that offset, read 8 bytes. You get the physical frame number.

## 4 — first attempt (broken)

We put `num` on the stack. Both parent and child call `get_pfn(&num)` and `printf`. Simple.

[src/failed_01_both_print.c](src/failed_01_both_print.c)

Output:
```
child  pid=45188  PFN_BEFORE_WRITE = 0x348bf6  num=100
child  pid=45188  PFN_AFTER_WRITE  = 0x348bf6  num=42
SAME frame (no COW yet?)
parent pid=45187  PFN              = 0x34b6b0  num=100
```

Parent PFN = `0x34b6b0`. Child PFN = `0x348bf6`. Already different. We never saw them share.

Why: `printf` calls `write()` which touches the stdio buffer. The stdio buffer is on the heap, which was shared. That write hit the read-only bit → COW fired → page split. By the time `get_pfn` ran, the pages had already been copied.

And the child says "SAME frame" before and after its write — because the parent's `printf` already triggered the split. The child was the sole owner by then (refcount=1), so the kernel had already restored write=1. No second trap needed.

The measurement destroyed the thing we were measuring.

## 5 — second attempt (still broken)

We removed `printf` from the child. The child sends its PFN values through a pipe instead — raw bytes, no stdio. Only the parent prints.

[src/failed_02_pipe_still_stack.c](src/failed_02_pipe_still_stack.c)

Output:
```
parent PFN (pre-fork):     0x34737c
parent PFN (post-fork):    0x413cf5
child  PFN (before write): 0x34737c
child  PFN (after write):  0x34737c

BEFORE WRITE: parent PFN != child PFN -> ALREADY SPLIT
AFTER WRITE:  child PFN unchanged (stack page was already private)
```

Better — no printf in the child. But the parent's PFN still changed: `0x34737c` → `0x413cf5` between pre-fork and post-fork.

Why: `get_pfn()` calls `pread(fd, &entry, 8, offset)`. `pread` writes 8 bytes to `&entry` — a local variable at `[rbp-8]`. `num` lives at `[rbp-4]`. Same 4096-byte page. The parent's `pread` wrote to the stack → hit write=0 → COW fired → parent got a new frame. The child kept the original (`0x34737c`).

The child's PFN before and after the write stayed `0x34737c` for the same reason as attempt 1 — by the time the child ran, it was already the sole owner.

The proof that they shared is hiding in the numbers: child PFN `0x34737c` = parent pre-fork PFN `0x34737c`. The child inherited the original frame. But we can't show simultaneous sharing because any observation from either process writes to the shared page.

## 6 — the problem

`num` lives on the stack. `get_pfn()` also writes to the stack (`&entry`, `&fd`, return addresses from function calls). Same 4096-byte page. Every observation is a write. Every write triggers COW.

You cannot read the physical frame number of a page without writing to that page, if your measurement variables live on the same page as the thing you're measuring.

This is not a bug. This is the mechanism working exactly as designed. The page table doesn't care why you wrote — whether it was your `num = 42` or `pread`'s `&entry` or `printf`'s buffer flush. A write is a write. Bit 1 is 0. Interrupt 14 fires.

## 7 — the fix

Two changes:

1. Put `num` on its own page using `mmap(NULL, 4096, ...)`. Now `num` lives at some address like `0x7f...000` and the stack lives at `0x7ffd...000`. Different pages. Writing to the stack doesn't touch `num`'s page.

2. Have only the parent read both pagemaps — its own `/proc/self/pagemap` and `/proc/<child_pid>/pagemap`. The child blocks on a pipe and does nothing. Zero writes from the child.

[src/03_pfn_proof.c](src/03_pfn_proof.c)

Output:
```
=== BEFORE ANY WRITE ===
parent PFN = 0x401c07   (pid 42541)
child  PFN = 0x401c07   (pid 42542)
SAME physical frame: 0x401c07 * 4096 = phys 0x401c07000

=== AFTER CHILD WRITES num=42 ===
parent PFN = 0x401c07   num=100
child  PFN = 0x268b1f
DIFFERENT frames -> COW split confirmed
```

`0x401c07` = `0x401c07`. Same number. Same DRAM row. Two processes, one physical copy. No disturbance from the measurement.

After `num = 42`: child moved to `0x268b1f`. The kernel allocated a new 4096-byte frame, copied the old one into it, then the child's store landed on the new copy. Parent stayed at `0x401c07`.

## 8

But pagemap only shows the final frame number. The CPU doesn't jump straight to it. It walks through 4 tables, each one a 4096-byte array of 8-byte entries. The virtual address is split into 5 fields — four table indexes and a byte offset.

```
Virtual address: 0x7526eab4f000

bits 47-39   index into table 1 (PGD)     = 234
bits 38-30   index into table 2 (PUD)     = 155
bits 29-21   index into table 3 (PMD)     = 341
bits 20-12   index into table 4 (PTE)     = 335
bits 11-0    byte offset in the frame     = 0x000
```

The CPU reads the CR3 register → that's the physical address of table 1. It reads entry 234 from that table → gets the physical address of table 2. Reads entry 155 → table 3. Entry 341 → table 4. Entry 335 → the final 64-bit integer with the frame number.

To see every level, we need ring 0. We wrote a kernel module.

## 9

The module ([src/ptwalk.c](src/ptwalk.c)) takes a pid and a virtual address. It finds that process's `task_struct`, gets its `mm_struct` (which holds the CR3 value), and walks all four levels. It prints every entry's raw hex value, index, and the final frame number with permission bits. It exposes the result at `/proc/ptwalk`.

```bash
sudo insmod ptwalk.ko target_pid=43451 target_va=128810006933504
sudo cat /proc/ptwalk
sudo rmmod ptwalk
```

Parent (pid 43451):
```
CR3(pgd phys) = 0x9c672000

PGD val = 0x2034c067   (index 234)
P4D val = 0x2034c067
PUD val = 0x2034d067   (index 155)
PMD val = 0x21ea7067   (index 341)
PTE val = 0x840000006f16a825   (index 335)
PFN     = 0x6f16a     phys = 0x6f16a000
present = 1   write = 0
```

Child (pid 43452):
```
CR3(pgd phys) = 0x21ea6000

PGD val = 0x1f7b1067   (index 234)
P4D val = 0x1f7b1067
PUD val = 0x1948a067   (index 155)
PMD val = 0x209a8067   (index 341)
PTE val = 0x840000006f16a805   (index 335)
PFN     = 0x6f16a     phys = 0x6f16a000
present = 1   write = 0
```

Every table at every level is a different physical page. CR3 differs. PGD entries differ. PUD entries differ. PMD entries differ. The PTE values differ (`0x...825` vs `0x...805`).

But bits 12-51 of both PTEs encode `0x6f16a`. Same frame. Same DRAM row. Two separate page table trees, four levels deep, converging on one physical address.

Both have `write = 0`. The trap is armed. The first process to store a byte here will fault.

## 10

To prove the trap fires, we don't need to read pagemap. We can watch the kernel function that handles it.

The kernel calls `do_wp_page()` when a write hits a read-only page that was once writable. We attached a probe to that function — a kernel module that prints to `dmesg` every time `do_wp_page` runs for our process.

[src/kprobe_cow.c](src/kprobe_cow.c)

```
[ 1463.026538] Planted kprobe at do_wp_page
[ 1463.028688] COW TRAP: do_wp_page called by cow_test (pid: 42109)
[ 1463.029032] COW TRAP: do_wp_page called by cow_test (pid: 42110)
[ 1463.029051] COW TRAP: do_wp_page called by cow_test (pid: 42110)
...
[ 1463.057408] kprobe at do_wp_page unregistered
```

pid 42109 = parent. pid 42110 = child. Multiple traps — not just `num`, but stack pages, libc data pages, everything that was shared and then written.

## 11

We also traced `fork()` itself. The kernel function is `kernel_clone`. We attached both a pre-handler (fires when `kernel_clone` starts) and a return-handler (fires when it finishes, captures the return value).

[src/kprobe_fork.c](src/kprobe_fork.c)

```
[ 1253.617880] Planted kprobe and kretprobe at kernel_clone
[ 1253.620528] kprobe: kernel_clone called by fork_test (pid: 40282)
[ 1253.620603] kretprobe: kernel_clone returned 40283 for fork_test (pid: 40282)
[ 1253.666400] kprobe and kretprobe at kernel_clone unregistered
```

One call. Return value 40283 = child pid. In the child's context, the saved `rax` was set to 0. Two tasks, one `kernel_clone`, divergence in one register.

## 12

Same thing without a kernel module, using eBPF from userspace:

```bash
sudo bpftrace -c ./fork_test -e '
  kprobe:kernel_clone /comm == "fork_test"/ {
    printf("clone called by %d\n", pid);
  }
  kretprobe:kernel_clone /comm == "fork_test"/ {
    printf("clone returned %d\n", retval);
  }'
```

```
ebpf: clone called by 40345
ebpf: clone returned 40346
value in child process: 100
value in parent process: 100
```

No module to compile. No `insmod`. One command. Same proof.

---

## What happened

```
BEFORE fork():

   process 40282
        |
        v
   CR3 ---> [ table 1 ] ---> [ table 2 ] ---> [ table 3 ] ---> [ table 4 ]
                                                                     |
                                                              PTE: write=1
                                                              PFN: 0x6f16a
                                                                     |
                                                                     v
                                                            DRAM row 0x6f16a
                                                            [ num = 100 ]


AFTER fork(), BEFORE any write:

   process 43451 (parent)                    process 43452 (child)
        |                                         |
        v                                         v
   CR3=0x9c672000                            CR3=0x21ea6000
        |                                         |
        v                                         v
   [ PGD 0x2034c067 ]                       [ PGD 0x1f7b1067 ]
        |                                         |
        v                                         v
   [ PUD 0x2034d067 ]                       [ PUD 0x1948a067 ]
        |                                         |
        v                                         v
   [ PMD 0x21ea7067 ]                       [ PMD 0x209a8067 ]
        |                                         |
        v                                         v
   PTE: 0x...6f16a825                       PTE: 0x...6f16a805
   PFN=0x6f16a, write=0                    PFN=0x6f16a, write=0
        |                                         |
        +------------------+----------------------+
                           |
                           v
                   DRAM row 0x6f16a
                   [ num = 100 ]
                   refcount = 2


AFTER child writes num = 42:

   process (parent)                          process (child)
        |                                         |
   PFN=0x401c07, write=1                    PFN=0x268b1f, write=1
        |                                         |
        v                                         v
   DRAM row 0x401c07                        DRAM row 0x268b1f
   [ num = 100 ]                            [ num = 42 ]
   refcount = 1                             refcount = 1
```

---

## Run it yourself

```bash
# compile everything
cd src
gcc -o fork_test 01_fork_test.c
gcc -o cow_test 02_cow_test.c
gcc -o pfn_proof 03_pfn_proof.c
gcc -o cow_hold 04_cow_hold.c
make   # builds kernel modules

# proof 1: fork trace
sudo dmesg -c > /dev/null
sudo insmod kprobe_fork.ko
./fork_test
sudo rmmod kprobe_fork
sudo dmesg

# proof 2: write trap
sudo dmesg -c > /dev/null
sudo insmod kprobe_cow.ko
./cow_test
sudo rmmod kprobe_cow
sudo dmesg

# proof 3: same frame before, different after
sudo ./pfn_proof

# proof 4: full page table walk from ring 0
./cow_hold &
# (it prints the insmod commands — run them)
sudo insmod ptwalk.ko target_pid=<PARENT> target_va=<ADDR>
sudo cat /proc/ptwalk
sudo rmmod ptwalk
sudo insmod ptwalk.ko target_pid=<CHILD> target_va=<ADDR>
sudo cat /proc/ptwalk
sudo rmmod ptwalk

# proof 5: ebpf one-liner (no modules needed)
sudo bpftrace -c ./fork_test -e '
  kprobe:kernel_clone /comm == "fork_test"/ {
    printf("clone called by %d\n", pid);
  }
  kretprobe:kernel_clone /comm == "fork_test"/ {
    printf("clone returned %d\n", retval);
  }'
```

## What you need

- Linux x86_64 with kernel headers installed (`apt install linux-headers-$(uname -r)`)
- gcc, make
- bpftrace (for proof 5)
- sudo (page table reads and kernel modules need ring 0)

## License

MIT
