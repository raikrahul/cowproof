#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/pgtable.h>

static int target_pid = 0;
module_param(target_pid, int, 0444);

static unsigned long target_va = 0;
module_param(target_va, ulong, 0444);

static int ref_show(struct seq_file *m, void *v)
{
    struct pid *pid_struct;
    struct task_struct *task;
    struct mm_struct *mm;
    pgd_t *pgd; p4d_t *p4d; pud_t *pud; pmd_t *pmd; pte_t *pte;
    unsigned long pfn;
    struct page *page;

    pid_struct = find_get_pid(target_pid);
    if (!pid_struct) return 0;

    task = get_pid_task(pid_struct, PIDTYPE_PID);
    put_pid(pid_struct);
    if (!task) return 0;

    mm = get_task_mm(task);
    if (!mm) {
        put_task_struct(task);
        return 0;
    }

    mmap_read_lock(mm);
    pgd = pgd_offset(mm, target_va);
    if (!pgd_none(*pgd) && !pgd_bad(*pgd)) {
        p4d = p4d_offset(pgd, target_va);
        if (!p4d_none(*p4d) && !p4d_bad(*p4d)) {
            pud = pud_offset(p4d, target_va);
            if (!pud_none(*pud) && !pud_bad(*pud)) {
                pmd = pmd_offset(pud, target_va);
                if (!pmd_none(*pmd) && !pmd_bad(*pmd)) {
                    pte = pte_offset_kernel(pmd, target_va);
                    if (!pte_none(*pte) && pte_present(*pte)) {
                        pfn = pte_pfn(*pte);
                        if (pfn_valid(pfn)) {
                            page = pfn_to_page(pfn);
                            seq_printf(m, "PID=%d VA=0x%lx PFN=0x%lx REFCOUNT=%d R/W=%d\n",
                                       target_pid, target_va, pfn, page_ref_count(page), pte_write(*pte));
                        }
                    }
                }
            }
        }
    }
    mmap_read_unlock(mm);
    mmput(mm);
    put_task_struct(task);
    return 0;
}

static int ref_open(struct inode *inode, struct file *file) {
    return single_open(file, ref_show, NULL);
}

static const struct proc_ops ref_ops = {
    .proc_open    = ref_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static int __init ref_init(void) {
    proc_create("refproof", 0444, NULL, &ref_ops);
    return 0;
}

static void __exit ref_exit(void) {
    remove_proc_entry("refproof", NULL);
}

module_init(ref_init)
module_exit(ref_exit)
MODULE_LICENSE("GPL");
