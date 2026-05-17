#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/pgtable.h>
#include <asm/io.h>

/* walks all 4 levels of page table for a given virtual address */
/* prints PGD/P4D/PUD/PMD/PTE entries and their physical addresses */
static void walk_page_table(struct seq_file *m, struct mm_struct *mm,
                            unsigned long va, const char *label)
{
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    unsigned long pfn;

    seq_printf(m, "\n=== %s  VA=0x%lx ===\n", label, va);

    pgd = pgd_offset(mm, va);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        seq_printf(m, "  PGD: NONE/BAD\n");
        return;
    }
    seq_printf(m, "  PGD val=0x%lx  (index %lu)\n",
               pgd_val(*pgd), pgd_index(va));

    p4d = p4d_offset(pgd, va);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        seq_printf(m, "  P4D: NONE/BAD\n");
        return;
    }
    seq_printf(m, "  P4D val=0x%lx\n", p4d_val(*p4d));

    pud = pud_offset(p4d, va);
    if (pud_none(*pud) || pud_bad(*pud)) {
        seq_printf(m, "  PUD: NONE/BAD\n");
        return;
    }
    seq_printf(m, "  PUD val=0x%lx  (index %lu)\n",
               pud_val(*pud), pud_index(va));

    pmd = pmd_offset(pud, va);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) {
        seq_printf(m, "  PMD: NONE/BAD\n");
        return;
    }
    seq_printf(m, "  PMD val=0x%lx  (index %lu)\n",
               pmd_val(*pmd), pmd_index(va));

    pte = pte_offset_kernel(pmd, va);
    if (pte_none(*pte)) {
        seq_printf(m, "  PTE: NONE\n");
        return;
    }

    pfn = pte_pfn(*pte);
    seq_printf(m, "  PTE val=0x%lx  (index %lu)\n", pte_val(*pte), pte_index(va));
    seq_printf(m, "  PFN = 0x%lx  phys = 0x%lx\n", pfn, pfn << PAGE_SHIFT);
    seq_printf(m, "  present=%d  write=%d  dirty=%d  young=%d\n",
               pte_present(*pte), pte_write(*pte),
               pte_dirty(*pte), pte_young(*pte));
}

/* target_pid: set via module param, walk this process's page table */
static int target_pid = 0;
module_param(target_pid, int, 0444);

/* target_va: virtual address to walk */
static unsigned long target_va = 0;
module_param(target_va, ulong, 0444);

static int ptwalk_show(struct seq_file *m, void *v)
{
    struct pid *pid_struct;
    struct task_struct *task;
    struct mm_struct *mm;

    pid_struct = find_get_pid(target_pid);
    if (!pid_struct) {
        seq_printf(m, "pid %d not found\n", target_pid);
        return 0;
    }

    task = get_pid_task(pid_struct, PIDTYPE_PID);
    put_pid(pid_struct);
    if (!task) {
        seq_printf(m, "no task for pid %d\n", target_pid);
        return 0;
    }

    mm = task->mm;
    if (!mm) {
        seq_printf(m, "pid %d has no mm (kernel thread?)\n", target_pid);
        put_task_struct(task);
        return 0;
    }

    seq_printf(m, "pid=%d  comm=%s  CR3(pgd phys)=0x%lx\n",
               target_pid, task->comm,
               (unsigned long)virt_to_phys(mm->pgd));

    mmap_read_lock(mm);
    walk_page_table(m, mm, target_va, "TARGET");
    mmap_read_unlock(mm);

    put_task_struct(task);
    return 0;
}

static int ptwalk_open(struct inode *inode, struct file *file)
{
    return single_open(file, ptwalk_show, NULL);
}

static const struct proc_ops ptwalk_ops = {
    .proc_open    = ptwalk_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static int __init ptwalk_init(void)
{
    proc_create("ptwalk", 0444, NULL, &ptwalk_ops);
    pr_info("ptwalk: loaded, target_pid=%d target_va=0x%lx\n",
            target_pid, target_va);
    return 0;
}

static void __exit ptwalk_exit(void)
{
    remove_proc_entry("ptwalk", NULL);
    pr_info("ptwalk: unloaded\n");
}

module_init(ptwalk_init)
module_exit(ptwalk_exit)
MODULE_LICENSE("GPL");
