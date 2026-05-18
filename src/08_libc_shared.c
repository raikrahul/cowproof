#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>

void segv_handler(int sig) {
    printf("[Child] SIGSEGV caught! No CoW happened. Exiting.\n");
    exit(0);
}

uint64_t get_pfn(void *vaddr) {
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) {
        perror("open pagemap");
        exit(1);
    }
    
    uint64_t offset = ((uintptr_t)vaddr / 4096) * 8;
    uint64_t entry = 0;
    
    if (pread(fd, &entry, 8, offset) != 8) {
        perror("pread");
        close(fd);
        exit(1);
    }
    close(fd);
    
    // Print raw entry for debugging
    printf("[%d] raw pagemap entry: 0x%lx (present=%d)\n", getpid(), entry, !!(entry & (1ULL << 63)));
    
    return entry & ((1ULL << 55) - 1); // Bits 0-54
}

#include <dlfcn.h>

int main() {
    void *printf_addr = dlsym(RTLD_NEXT, "printf");
    if (!printf_addr) {
        // Fallback if RTLD_NEXT fails
        printf_addr = dlsym(RTLD_DEFAULT, "printf");
    }
    
    // Force the page to be present in parent
    volatile char dummy = *(char*)printf_addr;
    
    printf("[Parent] printf virtual address: %p\n", printf_addr);
    
    uint64_t parent_pfn = get_pfn(printf_addr);
    printf("[Parent] printf PFN: 0x%lx\n", parent_pfn);
    
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    
    if (pid == 0) {
        // Child
        // Force the page to be present in child's page table by reading it
        volatile char dummy2 = *(char*)printf_addr;
        
        uint64_t child_pfn = get_pfn(printf_addr);
        printf("[Child]  printf PFN: 0x%lx\n", child_pfn);
        
        if (child_pfn == parent_pfn) {
            printf("[Child]  SAME PFN. Sharing page-cache frame.\n");
        }
        
        // Setup SIGSEGV handler
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = segv_handler;
        sigaction(SIGSEGV, &sa, NULL);
        
        printf("[Child]  Attempting to write to printf. If CoW, it will change PFN. If VM_WRITE=0, it will SIGSEGV.\n");
        
        // Attempt to write
        *(char *)printf_addr = 0x90; // Write a NOP
        
        printf("[Child]  Wait, we successfully wrote?! (This should not happen)\n");
        exit(1);
    } else {
        // Parent
        wait(NULL);
    }
    
    return 0;
}
