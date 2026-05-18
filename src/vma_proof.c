#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pid.h>
#include <asm/pgtable.h>

static int target_pid = 0;
static unsigned long target_va = 0;

module_param(target_pid, int, 0444);
module_param(target_va, ulong, 0444);

static int __init vma_proof_init(void)
{
    struct task_struct *task;
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;

    if (!target_pid || !target_va) {
        pr_err("target_pid and target_va required\n");
        return -EINVAL;
    }

    task = get_pid_task(find_vpid(target_pid), PIDTYPE_PID);
    if (!task) {
        pr_err("Task not found\n");
        return -EINVAL;
    }

    mm = get_task_mm(task);
    if (!mm) {
        pr_err("No mm_struct\n");
        put_task_struct(task);
        return -EINVAL;
    }

    mmap_read_lock(mm);

    vma = find_vma(mm, target_va);
    if (!vma || vma->vm_start > target_va) {
        pr_err("No VMA found for %lx\n", target_va);
        goto out;
    }

    pr_info("VMA for %lx: start=%lx, end=%lx\n", target_va, vma->vm_start, vma->vm_end);
    pr_info("VMA flags: VM_READ=%d, VM_WRITE=%d, VM_EXEC=%d\n",
            !!(vma->vm_flags & VM_READ),
            !!(vma->vm_flags & VM_WRITE),
            !!(vma->vm_flags & VM_EXEC));

    /* Page table walk to get PTE R/W */
    pgd = pgd_offset(mm, target_va);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) goto out;
    p4d = p4d_offset(pgd, target_va);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) goto out;
    pud = pud_offset(p4d, target_va);
    if (pud_none(*pud) || pud_bad(*pud)) goto out;
    pmd = pmd_offset(pud, target_va);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) goto out;
    pte = pte_offset_kernel(pmd, target_va);

    pr_info("PTE hardware R/W bit: %d\n", pte_write(*pte));
    pr_info("PTE PFN: 0x%lx\n", pte_pfn(*pte));

out:
    mmap_read_unlock(mm);
    mmput(mm);
    put_task_struct(task);
    return 0;
}

static void __exit vma_proof_exit(void)
{
    pr_info("vma_proof unloaded\n");
}

module_init(vma_proof_init);
module_exit(vma_proof_exit);
MODULE_LICENSE("GPL");
