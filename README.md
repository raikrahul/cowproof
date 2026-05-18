# cowproof

When you call `fork()`, does your data get copied? Every OS textbook says "copy on write." None of them prove it. This repo does — with kernel modules, ring 0 page table walks, and live DRAM addresses from a real machine.

Kernel: `7.0.0-070000rc4-generic` (x86_64). All code compiles and runs. Logs are real.

By Rahul — systems writing that proves what textbooks assert.

What you'll have measured by the end:

- 9 proofs of copy-on-write, run on real hardware
- 4 ring 0 kernel modules (page table walk, refcount, VMA flag check, write trap)
- 2 eBPF / kprobe paths (one of them needs no module load)
- Real 4-level page table walks: CR3, PGD, PUD, PMD, PTE — every entry in hex
- Real PFNs from two processes: same number before write, different number after
- The "measurement disturbs the thing measured" lesson, from two failed attempts that ship in this repo
- A working SIGSEGV when you try to write to a `libc.so` `.text` page

Follow: <https://github.com/raikrahul> · Sponsor: <https://github.com/sponsors/raikrahul>

---

## 1. The Question

You have a number in memory.

```c
int num = 100;
```

You call `fork()`. Now two processes exist. The CPU put `56` (clone) into `rax`, executed `syscall`, switched to ring 0. The kernel made a new `task_struct`. It returned the child's pid to the parent in `rax`, and `0` to the child in `rax`. Both resume at the same instruction.

The question: when both processes read `num`, are they reading the same bytes from the same DRAM chip? Or did the kernel copy 4096 bytes of memory?

## 2. The Split

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

## 3. The Physical Frame

We need to see the physical address. Not the virtual one you get from `&num` — the actual DRAM row.

The CPU stores a 64-bit integer for every 4096-byte page. This integer lives in a table in DRAM. Bits 12-51 of that integer hold the physical frame number. Bit 1 says whether writes are allowed. Bit 0 says whether the page exists in DRAM at all.

```
64-bit entry in the page table:

bit 0       1 = exists in DRAM
bit 1       1 = writes allowed, 0 = trap on write
bits 12-51  physical frame number (which 4096-byte row of DRAM)
```

The file `/proc/self/pagemap` exposes this. You divide a virtual address by 4096, multiply by 8, seek to that offset, read 8 bytes. You get the physical frame number.

## 4. First Attempt (Broken)

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

## 5. Second Attempt (Still Broken)

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

## 6. The Measurement Problem

`num` lives on the stack. `get_pfn()` also writes to the stack (`&entry`, `&fd`, return addresses from function calls). Same 4096-byte page. Every observation is a write. Every write triggers COW.

You cannot read the physical frame number of a page without writing to that page, if your measurement variables live on the same page as the thing you're measuring.

This is not a bug. This is the mechanism working exactly as designed. The page table doesn't care why you wrote — whether it was your `num = 42` or `pread`'s `&entry` or `printf`'s buffer flush. A write is a write. Bit 1 is 0. Interrupt 14 fires.

## 7. The Memory Isolation Fix

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

## 8. The Page Table Walk

But pagemap only shows the final frame number. The CPU doesn't jump straight to it. It walks through 4 tables, each one a 4096-byte array of 8-byte entries. The virtual address is split into 5 fields — four table indexes and a byte offset.

```
Virtual address: 0x7526eab4f000

Top 48 bits sliced into the 5 fields the CPU consumes:

  011101010   010011011   101010101   101001111   000000000000
  bits 47-39  bits 38-30  bits 29-21  bits 20-12  bits 11-0
  PGD index   PUD index   PMD index   PTE index   byte offset
  = 234       = 155       = 341       = 335       = 0
```

Each table index is 9 bits because each table holds 2^9 = 512 entries × 8 bytes
= 4096 bytes = exactly one page. The bottom 12 bits are not an index; they're a
byte offset inside the 4096-byte physical frame the PTE points at.

The CPU reads the CR3 register → that's the physical address of table 1. It reads entry 234 from that table → gets the physical address of table 2. Reads entry 155 → table 3. Entry 341 → table 4. Entry 335 → the final 64-bit integer with the frame number.

To see every level, we need ring 0. We wrote a kernel module.

## 9. Ring 0 Verification

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

## 10. The Trap Handling

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

## 11. Tracing the Fork

We also traced `fork()` itself. The kernel function is `kernel_clone`. We attached both a pre-handler (fires when `kernel_clone` starts) and a return-handler (fires when it finishes, captures the return value).

[src/kprobe_fork.c](src/kprobe_fork.c)

```
[ 1253.617880] Planted kprobe and kretprobe at kernel_clone
[ 1253.620528] kprobe: kernel_clone called by fork_test (pid: 40282)
[ 1253.620603] kretprobe: kernel_clone returned 40283 for fork_test (pid: 40282)
[ 1253.666400] kprobe and kretprobe at kernel_clone unregistered
```

One call. Return value 40283 = child pid. In the child's context, the saved `rax` was set to 0. Two tasks, one `kernel_clone`, divergence in one register.

## 12. Userspace Verification

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

## Summary of Operations

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

### Driver 1: refcount

```
mmap(4096) MAP_PRIVATE → *num = 100 → fork()
→ refcount = 2 on physical frame 0x401c07
→ child writes 42 → #PF
→ alloc_page() → memcpy 4096 bytes → new frame 0x268b1f
→ parent: refcount=1 on 0x401c07
→ child:  refcount=1 on 0x268b1f
```

∴ a write splits the page and decrements the original frame's refcount.

### Driver 2: read/write asymmetry

```
fork() clears PTE R/W bit to 0 on writable user pages
→ child instruction fetch (read) = 0 traps                  ✓
→ child reads variable            = 180 cycles              ✓
→ child writes variable           = 16,140 cycles           ✗
→ kretprobe on do_wp_page         = 5,700 cycles in ring 0  ✗
```

∴ reads cost 0 traps; writes cost ~5,700 cycles inside the kernel handler.
∴ the hardware checks the R/W bit only on writes — reads always pass when P=1.

### Driver 3: `libc.so` `.text` sharing (no CoW involved)

```
dlsym(printf) → get_pfn() in parent  → PFN = 0x10e2b5
→ fork() → child get_pfn()           → PFN = 0x10e2b5
```

∴ child reads the same page-cache frame as the parent. Same physical bytes.

```
child writes *(char*)printf = 0x90 (NOP)
→ SIGSEGV caught                                       ✗
→ vma_proof.ko: find_vma() finds vm_flags & VM_WRITE = 0
```

∴ PTE R/W=0 **and** VMA has no VM_WRITE → write is illegal, not a CoW
→ kernel sends SIGSEGV instead of allocating a new frame.
∴ the PFN stays the same forever — shared without CoW.

---

## Reproduction Steps

```bash
# compile everything
cd src
gcc -o fork_test 01_fork_test.c
gcc -o cow_test 02_cow_test.c
gcc -o pfn_proof 03_pfn_proof.c
gcc -o cow_hold 04_cow_hold.c
gcc -o cow_refcount 05_cow_refcount_test.c
gcc -o cow_speed 07_cow_speed.c
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
# cow_hold mmaps a page, forks, prints the exact commands you need, then both
# processes sleep for 30 s so the module has time to read them. Output is like:
#
#   sudo insmod ptwalk.ko target_pid=43451 target_va=128810006933504
#   sudo cat /proc/ptwalk        # parent walk
#   sudo rmmod ptwalk
#   sudo insmod ptwalk.ko target_pid=43452 target_va=128810006933504
#   sudo cat /proc/ptwalk        # child walk
#   sudo rmmod ptwalk
#
# Copy-paste those four lines. Compare CR3, the four indices, and the PFN.
# Both PFNs match before any write; only the PTE bits differ.

# proof 5: ebpf one-liner (no modules needed)
sudo bpftrace -c ./fork_test -e '
  kprobe:kernel_clone /comm == "fork_test"/ {
    printf("clone called by %d\n", pid);
  }
  kretprobe:kernel_clone /comm == "fork_test"/ {
    printf("clone returned %d\n", retval);
  }'

# proof 6: refcount split
sudo dmesg -c > /dev/null
sudo insmod refcount_proof.ko
./cow_refcount
sudo rmmod refcount_proof
sudo dmesg

# proof 7: read vs write cost
sudo dmesg -c > /dev/null
sudo insmod kprobe_fault.ko
./cow_speed
sudo rmmod kprobe_fault
sudo dmesg

# proof 8: libc page cache sharing and SEGV
sudo gcc -o 08_libc_shared 08_libc_shared.c -ldl
sudo ./08_libc_shared

# proof 9: vma flags kernel test
sudo dmesg -c > /dev/null
sudo insmod vma_proof.ko target_pid=$$ target_va=0x$(cat /proc/self/maps | grep libc.so | head -n 1 | awk -F'-' '{print $1}')
sudo rmmod vma_proof
sudo dmesg

# proof 10: cross-process PFN read (any two PIDs sharing libc.so)
gcc -o libc_shared_pfn 10_libc_shared_pfn.c
sudo ./libc_shared_pfn $(pgrep -n bash)
# Reads /proc/self/maps + /proc/<other>/maps to locate the libc r-xp VMA in
# each process, then reads PFN from /proc/<pid>/pagemap for both. Expected:
# same PFN value in two processes that have no fork relationship — proof that
# page-cache sharing is the mechanism, not CoW.
```


## Prerequisites

- Linux x86_64 with kernel headers installed (`apt install linux-headers-$(uname -r)`)
- gcc, make
- bpftrace (for proof 5)
- sudo (page table reads and kernel modules need ring 0)

## 13. Confusions cleared

A real reader walked this proof in one session. The points below are the spots
where the picture broke and how the bytes resolved each one.

### Q&A in real bits

```
Q: parent and child have different CR3. Do they share page tables?
   CR3_parent = 0x9c672000   CR3_child = 0x21ea6000     ✗ different physical pages
   PGD_parent[234] = 0x2034c067   PGD_child[234] = 0x1f7b1067   ✗ different entries
   PTE_parent.PFN  = 0x6f16a      PTE_child.PFN  = 0x6f16a       ✓ same PFN
∴ tables at different physical addresses; only the leaf PFN value coincides at fork.

Q: &num same VA in both processes?
   parent &num = 0x7526eab4f000   child &num = 0x7526eab4f000     ✓ same
   address space layout cloned bit-for-bit by fork → same RSP → same compiler-chosen offset
∴ VA same, page walks different, PFN same → both VAs land on one DRAM row at fork.

Q: is there a CoW bit in the PTE?
   x86_64 PTE bits used by hardware: P R/W U/S PWT PCD A D PS G NX PFN
   Linux software bits in PTE: SOFT_DIRTY, SPECIAL, PROTNONE, UFFD_WP
   ✗ no CoW bit
   kernel decides CoW at fault time from:
     PTE R/W == 0  AND  vma->vm_flags & VM_WRITE       → CoW fault
     PTE R/W == 0  AND  !(vma->vm_flags & VM_WRITE)    → SIGSEGV
∴ "CoW" is a fault classification, not a stored bit.

Q: does R/W=0 block reads?
   CPU permission check on memory access:
     access type = read   → check P only (and U/S vs CS.RPL)
     access type = write  → check P + R/W
   PTE = P=1 R/W=0 U/S=1
   read instruction  → 0 traps         ✓
   write instruction → #PF vector 14   ✗
∴ asymmetric. reads pass; only writes trap. lazy CoW depends on this.

Q: if hardware checked R/W on reads too, would lazy CoW still work?
   instruction fetch is a read.  .text PTE has R/W=0.
   child's first mov after fork → fault → kernel copies .text page → flips R/W=1
   next instruction → fault again on next .text page
   ✗ child copies entire .text before executing one user instruction
   measured: do_wp_page costs 5,700 cycles in ring 0; normal load is 1-4 cycles
   ratio: 1,000× slower if every read faulted
∴ scheme is unworkable; reads must be free.

Q: U/S bit?
   bit 2 of PTE.
   1 = ring 3 may access     (user code, user data, user stack)
   0 = ring 0 only           (kernel half, vaddr ≥ 0xffff800000000000)
   ring 3 access of U/S=0 page → #PF, error code bit 2 (U) = 1
   kernel handler delivers SIGSEGV to the offending task.
∴ U/S fences kernel memory out of user reach without unmapping it.

Q: child "gets a new libc" after fork?
   fork mechanism = clone task_struct, alloc new PML4, copy PTEs entry by entry
   ✗ no mmap, ✗ no dlopen, ✗ no library reload
   child's PTE for libc.so .text VA = copy of parent's PTE
   parent's PTE was already pointing at the page-cache frame
∴ same PFN inherited transitively. library reload happens only on execve, not fork.

Q: can I predict the exact PFN of a page?
   kernel frame allocator (buddy) hands out free PFNs at alloc time
   varies per boot, per run, per allocation timing
   ✗ unpredictable
   /proc/<pid>/pagemap exposes it: entry = (PFN bits 0..54) | (present << 63)
   non-root readers see PFN bits = 0  (rowhammer mitigation since kernel 4.0)
∴ run with sudo to read PFN; relationships (same/different) are predictable, values are not.

Q: parent blocks on read(). where do its registers live?
   user-mode rax..r15 = pushed to kernel stack as struct pt_regs at syscall entry
   kernel-resumption rsp = saved in parent->thread.sp inside thread_struct
   fs, gs (TLS) = saved in thread_struct
   in CPU registers right now = some OTHER task's state
   CR3 = some OTHER task's mm->pgd
∴ registers physically reside in DRAM, not silicon, while the task is descheduled.

Q: parent never calls wait(). child _exit(0). what happens?
   do_exit on child:
     exit_mm → mm refcount drops, possibly freed
     exit_files → all fds closed
     state = TASK_DEAD, exit_state = EXIT_ZOMBIE
     task_struct stays (8 KB), pid stays allocated
   parent exits later:
     forget_original_parent reparents zombie children to init (pid 1)
   init's wait4 loop reaps zombies → release_task → free task_struct → pid back in pool
∴ unwaited zombie lives until parent exits; init reaps; ~µs window in short programs.
```

### Mistakes made this session (line → what went wrong → what should be → why sloppy → what missed → how to prevent)

```
M1  "different CR3 but they contain same things because both point to the same table"
    wrong: tables are at different physical pages
    right: 8-byte entries inside encode the same PFN at fork; the table pages differ
    why sloppy: collapsed "table" and "table contents" into one word
    missed: a page table is itself a 4096-byte chunk in DRAM with its own PFN
    prevent: when saying "table" name which level (PML4/PUD/PMD/PTE) and ask
             "the physical page that holds this table" vs "the bytes inside it"

M2  "what is the CoW structure bit?  is this bit 58?"
    wrong: no such bit exists in Linux's PTE encoding
    right: kernel infers CoW from PTE R/W=0 AND VMA VM_WRITE=1; struct page _refcount
           decides copy vs flip
    why sloppy: assumed every kernel concept maps to one bit
    missed: kernel mechanism is layered (PTE + VMA + struct page); single-bit mental
            model under-captures it
    prevent: when asked "which bit?" first ask "is the mechanism in hardware bits,
             kernel software bits, VMA flags, or page metadata?"  often more than one

M3  "if R/W gated reads, scheme is workable; just copy and decrement refcount"
    wrong: every instruction fetch is a read; child's first mov faults; .text pages
           all have R/W=0; child copies entire .text before running anything
    right: lazy CoW requires reads to be free; only writes pay
    why sloppy: only considered data pages, not code pages
    missed: instruction fetches go through the same PTE permission check
    prevent: when reasoning about R/W behavior, think of the code segment (.text)
             which is always R/W=0 read-only

M4  "the PFN will differ because we have different ram address but all higher level
     translations will be same"
    wrong: inverted both halves
    right: higher-level table pages are at DIFFERENT physical addresses; their
           entry values are the same at fork; leaf PFN is the SAME value
    why sloppy: confused where a value lives with what value it holds
    missed: "different physical address" describes location of the table, not
            content of the table entry
    prevent: separate "the page that holds X" from "the value of X" in any
             page-table reasoning

M5  "after fork the child gets a new libc"
    wrong: fork inherits, does not reload
    right: only execve replaces the address space; fork clones it
    why sloppy: collapsed fork and exec into one operation
    missed: exec is the operation that mmaps the new ELF and its dependencies
    prevent: when claiming "the address space changed", ask "did execve run?"

M6  "read(pipe_fd[0], \"\", 1) works fine"
    wrong: destination "" is a string literal in .rodata; copy_to_user fails with
           EFAULT; read returns -1
    why-it-still-works: pipe_read blocks on empty buffer BEFORE attempting copy;
           wakeup happens before EFAULT; ignored return value masks the error
    why sloppy: used the first throwaway pointer at hand
    missed: kernel writes 1 byte to the user-supplied address; address must be
            writable from user perspective
    prevent: any syscall whose buffer the kernel writes into needs a stack-local
             (R/W=1, U/S=1) destination
             char c; read(fd, &c, 1);

M7  "i do not know this fflush trick"
    confusion: why does the child need fflush(stdout) between printf and the
               sync-pipe write?
    bytes: printf writes into a libc-managed buffer in YOUR heap (4096 bytes
           by default, malloc'd on first stdout use).
           The kernel's stdout pipe (fd 1) only receives bytes when libc
           calls write(1, buf, n). libc emits that on:
             (a) buffer full
             (b) explicit fflush(stdout)
             (c) atexit handler installed by exit(0)
           _exit(0) bypasses (c). _exit destroys the address space without
           running libc cleanup → bytes still in the heap buffer are LOST.
           Codio captures stdout via a pipe → fully buffered (line buffering
           is a terminal-only mode).
    wrong without fflush:
           child does printf → 21 bytes in libc buffer (heap, child-private)
           child does write(sync_pipe, "x", 1) → parent wakes
           parent does printf → bytes in parent's libc buffer
           parent returns from main → libc atexit → write(1, parent_buf, 24)
                → "Goodbye from the parent\n" in kernel stdout pipe
           child does _exit(0) → no libc cleanup → child's 21 bytes vanish
           Codio sees only parent's line → wrong output
    right: child sequence is printf, fflush(stdout), write(sync_pipe, ...),
           _exit(0). fflush emits write(1, buf, 21) FIRST, so the child's bytes
           are in the kernel pipe before the parent ever wakes.
    why sloppy: treated printf as if it called write(1, ...) directly.
    missed: two buffers in the path — libc's heap buffer and the kernel pipe
            buffer. Codio reads the kernel one.
    prevent: any printf whose bytes must arrive before a later event needs an
             explicit flush. Terminal line-buffering hides this in development;
             pipes/files behave differently.

M8  "codio failed me with 'Try not to use wait()' but my code has no wait() call"
    bytes: grep -i wait exercise_2.c hit three matches in COMMENTS:
             line  8: "Parent must NOT call wait()."           (spec restatement)
             line 25: "Expected output (deterministic, parent NEVER waits):"
             line 110: "No wait() call anywhere."              (own commentary)
           Codio's grader is doing source-text substring match — not AST,
           not strace, not ptrace. The match doesn't care about /* */.
    wrong assumption: comments are invisible to graders.
    right model: an auto-grader runs the cheapest static check first; substring
                 grep is cheaper than running the binary. Comments are part of
                 the file the regex sees.
    why sloppy: copy-pasted spec language into TODO comments without thinking
                about how the grader reads the file.
    missed: the grader's banned-API check sees every byte of the source,
            including documentation that refers to the API by name.
    prevent: when an auto-grader bans API X, also strip the literal string X
             from comments. Synonym or behavior description only.

M9  "i should be able to predict the exact PFN"
    bytes: kernel buddy allocator hands out free PFNs in whatever order frames
           became free. Influenced by:
             - boot time (different on every reboot)
             - other tasks alloc/free history
             - memory pressure
             - NUMA node placement
           same code, same input, different runs → different PFN values.
    wrong: assumed PFN = deterministic function of the source.
    right: PFN values are observations, not predictions. What IS predictable:
             - relationships ("parent and child agree at fork")
             - changes  ("after writer faults, PFNs differ")
             - constants ("libc.so .text PFN is stable across the whole boot
                          because the page-cache entry is sticky")
    why sloppy: pattern-matched PFN onto deterministic compiler outputs (e.g.
                offsets, symbol addresses) which ARE predictable.
    missed: anything the kernel allocates at runtime has non-deterministic
            naming; only file-backed mappings get stable identities (via inode +
            file offset → page cache).
    prevent: ask "is this from the linker/compiler or from the kernel?" Linker
             outputs are stable; kernel allocator outputs are not.
```

### New driver: cross-process PFN reader

```
src/10_libc_shared_pfn.c

Build: gcc -o libc_shared_pfn 10_libc_shared_pfn.c
Run:   sudo ./libc_shared_pfn <other_pid>

Mechanism:
  /proc/self/maps   → grep r-xp libc → start VA = first VA mapped to libc.so + 0
  /proc/<other>/maps → same → other process's start VA (different value due to
                       ASLR, same file offset)
  /proc/self/pagemap @ (self_va / 4096) * 8       → 8-byte entry; PFN = bits 0..54
  /proc/<other>/pagemap @ (other_va / 4096) * 8   → same

Expected output:
  self  PFN = 0x18b3a   phys = 0x18b3a000
  other PFN = 0x18b3a   phys = 0x18b3a000
  SAME physical frame: libc.so first .text page is shared.

Why same PFN with no fork relationship:
  kernel inode->i_mapping (struct address_space) holds (file_offset → struct page)
  every mmap(libc.so, ..., MAP_PRIVATE, offset 0) calls filemap_fault on first
  touch → find_get_page(mapping, 0) returns the single cached struct page →
  PTE points at its PFN. All N processes' PTEs end up pointing at one frame.

Failure mode without sudo:
  /proc/<pid>/pagemap returns PFN bits zeroed (rowhammer mitigation since 4.0).
  Result: both PFNs = 0, comparison meaningless. tool reports the error.
```

## More like this

Follow on GitHub: <https://github.com/raikrahul>
Sponsor: <https://github.com/sponsors/raikrahul>

If you want the rest of this style — process lifecycle, malloc internals, ext4
on-disk layout, all derived from bytes and registers, never from textbook
hand-waves — the sponsor page is where I'll be putting the longer pieces.

## License

MIT
