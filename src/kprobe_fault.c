#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/sched.h>

static struct kretprobe krp = {
    .kp.symbol_name = "do_wp_page",
    .data_size = sizeof(u64),
    .maxactive = 20,
};

static int entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    u64 *entry_tsc = (u64 *)ri->data;
    if (strcmp(current->comm, "cow_speed") == 0) {
        *entry_tsc = rdtsc();
    } else {
        *entry_tsc = 0;
    }
    return 0;
}

static int ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    u64 *entry_tsc = (u64 *)ri->data;
    if (*entry_tsc != 0) {
        u64 exit_tsc = rdtsc();
        pr_info("COW FAULT: do_wp_page for %s (pid: %d) cost %llu cycles\n",
                current->comm, current->pid, exit_tsc - *entry_tsc);
    }
    return 0;
}

static int __init kprobe_init(void)
{
    int ret;
    krp.entry_handler = entry_handler;
    krp.handler = ret_handler;
    ret = register_kretprobe(&krp);
    if (ret < 0) {
        pr_err("register_kretprobe failed, returned %d\n", ret);
        return ret;
    }
    pr_info("Planted kretprobe at do_wp_page\n");
    return 0;
}

static void __exit kprobe_exit(void)
{
    unregister_kretprobe(&krp);
    pr_info("kretprobe at do_wp_page unregistered\n");
}

module_init(kprobe_init)
module_exit(kprobe_exit)
MODULE_LICENSE("GPL");
