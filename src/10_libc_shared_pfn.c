/*
 * Prove: libc.so .text is the SAME physical frame across two processes.
 *
 * Mechanism:
 *   /proc/<pid>/maps      -> find the r-xp libc.so VMA (file offset 0 -> VA).
 *                            VA is different per process (ASLR), but both
 *                            point at the same file byte: libc.so + 0.
 *   /proc/<pid>/pagemap   -> 8-byte entry per VA page. Bit 63 = present.
 *                            Bits 0..54 = PFN. Needs CAP_SYS_ADMIN (sudo)
 *                            for the PFN bits to be non-zero; without root,
 *                            the kernel zeroes them out (rowhammer mitigation).
 *
 * Run:
 *   sudo ./libc_shared_pfn <other_pid>
 *
 *   <other_pid> can be any running process — bash, vim, whatever.
 *   `pidof bash` or `pidof vim` or `pgrep -n bash`.
 *
 * Expected: same PFN in both processes for libc.so's first .text page.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define PAGE 4096UL

/* Find the first r-xp VMA in pid whose path contains "libc".
 * Returns start VA. Also fills out_path with the file path.
 */
static int find_libc_text_va(pid_t pid, unsigned long *out_va,
                             char *out_path, size_t path_sz)
{
    char p[64];
    snprintf(p, sizeof p, "/proc/%d/maps", pid);
    FILE *f = fopen(p, "r");
    if (!f) { perror(p); return -1; }
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        unsigned long start, end;
        char perms[8], path[512] = "";
        int n = sscanf(line, "%lx-%lx %7s %*x %*x:%*x %*u %511[^\n]",
                       &start, &end, perms, path);
        if (n >= 3 && strcmp(perms, "r-xp") == 0 && strstr(path, "libc")) {
            *out_va = start;
            if (out_path) snprintf(out_path, path_sz, "%s", path);
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return -1;
}

/* PFN of VA in pid, from /proc/<pid>/pagemap. 0 if not present or no perms. */
static uint64_t pfn_of(pid_t pid, unsigned long va)
{
    char p[64];
    snprintf(p, sizeof p, "/proc/%d/pagemap", pid);
    int fd = open(p, O_RDONLY);
    if (fd < 0) { perror(p); return 0; }
    uint64_t entry = 0;
    off_t off = (off_t)(va / PAGE) * 8;
    if (pread(fd, &entry, 8, off) != 8) {
        perror("pread pagemap"); close(fd); return 0;
    }
    close(fd);
    if (!(entry & (1ULL << 63))) {
        fprintf(stderr, "VA 0x%lx in pid %d NOT present (bit 63 = 0)\n", va, pid);
        return 0;
    }
    return entry & ((1ULL << 55) - 1);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: sudo %s <other_pid>\n", argv[0]);
        return 1;
    }
    pid_t other = (pid_t)atoi(argv[1]);
    if (other <= 0) { fprintf(stderr, "bad pid\n"); return 1; }

    /* Force libc into memory by calling something from it. */
    printf("self pid = %d\n", getpid());

    unsigned long self_va = 0, other_va = 0;
    char self_path[512] = "", other_path[512] = "";

    if (find_libc_text_va(getpid(), &self_va, self_path, sizeof self_path) < 0) {
        fprintf(stderr, "self has no libc r-xp VMA\n"); return 1;
    }
    if (find_libc_text_va(other, &other_va, other_path, sizeof other_path) < 0) {
        fprintf(stderr, "pid %d has no libc r-xp VMA (or permission denied)\n", other);
        return 1;
    }

    printf("self  libc .text VA = 0x%lx  path=%s\n", self_va, self_path);
    printf("other libc .text VA = 0x%lx  path=%s\n", other_va, other_path);

    /* Touch self's first .text byte to ensure the page is faulted in. */
    volatile unsigned char touch = *(volatile unsigned char *)self_va;
    (void)touch;

    uint64_t self_pfn  = pfn_of(getpid(), self_va);
    uint64_t other_pfn = pfn_of(other,    other_va);

    printf("\nself  PFN = 0x%lx   phys = 0x%lx\n",
           self_pfn,  self_pfn  * PAGE);
    printf("other PFN = 0x%lx   phys = 0x%lx\n",
           other_pfn, other_pfn * PAGE);

    if (self_pfn == 0 || other_pfn == 0) {
        printf("\nPFN read failed — run with sudo (PFN bits zeroed for non-root).\n");
        return 1;
    }
    if (self_pfn == other_pfn) {
        printf("\nSAME physical frame: libc.so first .text page is shared.\n");
        printf("Two different VAs in two different page tables -> one DRAM frame.\n");
    } else {
        printf("\nDIFFERENT frames.  Check the paths above are the same libc.so;\n"
               "ASLR/different libc versions would explain this.\n");
    }
    return 0;
}
