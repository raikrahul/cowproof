#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/sched.h>

static struct kprobe kp = {
    .symbol_name = "kernel_clone",
};

static int __kprobes handler_pre(struct kprobe *p, struct pt_regs *regs)
{
    if (strcmp(current->comm, "fork_test") == 0) {
        pr_info("kprobe: kernel_clone called by %s (pid: %d)\n", current->comm, current->pid);
    }
    return 0;
}

static struct kretprobe krp = {
    .kp = {
        .symbol_name = "kernel_clone",
    },
};

static int __kprobes ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    if (strcmp(current->comm, "fork_test") == 0) {
        int retval = regs_return_value(regs);
        pr_info("kretprobe: kernel_clone returned %d for %s (pid: %d)\n", retval, current->comm, current->pid);
    }
    return 0;
}

static int __init kprobe_init(void)
{
    int ret;
    kp.pre_handler = handler_pre;
    ret = register_kprobe(&kp);
    if (ret < 0) {
        pr_err("register_kprobe failed, returned %d\n", ret);
        return ret;
    }
    
    krp.handler = ret_handler;
    krp.maxactive = 20;
    ret = register_kretprobe(&krp);
    if (ret < 0) {
        pr_err("register_kretprobe failed, returned %d\n", ret);
        unregister_kprobe(&kp);
        return ret;
    }
    pr_info("Planted kprobe and kretprobe at kernel_clone\n");
    return 0;
}

static void __exit kprobe_exit(void)
{
    unregister_kretprobe(&krp);
    unregister_kprobe(&kp);
    pr_info("kprobe and kretprobe at kernel_clone unregistered\n");
}

module_init(kprobe_init)
module_exit(kprobe_exit)
MODULE_LICENSE("GPL");
