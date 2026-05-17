#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/sched.h>

static struct kprobe kp = {
    .symbol_name = "do_wp_page",
};

static int __kprobes handler_pre(struct kprobe *p, struct pt_regs *regs)
{
    if (strcmp(current->comm, "cow_test") == 0) {
        pr_info("COW TRAP: do_wp_page called by %s (pid: %d)\n", current->comm, current->pid);
    }
    return 0;
}

static int __init kprobe_init(void)
{
    int ret;
    kp.pre_handler = handler_pre;
    ret = register_kprobe(&kp);
    if (ret < 0) {
        pr_err("register_kprobe for do_wp_page failed, returned %d\n", ret);
        return ret;
    }
    pr_info("Planted kprobe at do_wp_page\n");
    return 0;
}

static void __exit kprobe_exit(void)
{
    unregister_kprobe(&kp);
    pr_info("kprobe at do_wp_page unregistered\n");
}

module_init(kprobe_init)
module_exit(kprobe_exit)
MODULE_LICENSE("GPL");
