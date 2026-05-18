#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x09bf2a38, "single_open" },
	{ 0x0fc9dafd, "remove_proc_entry" },
	{ 0xa6aed43d, "find_get_pid" },
	{ 0x63a4468d, "get_pid_task" },
	{ 0x95ae2e24, "put_pid" },
	{ 0x44a8e760, "get_task_mm" },
	{ 0x63dc8f96, "__tracepoint_mmap_lock_start_locking" },
	{ 0x8efcc8cd, "down_read" },
	{ 0x63dc8f96, "__tracepoint_mmap_lock_acquire_returned" },
	{ 0xf296206e, "pgdir_shift" },
	{ 0xb1ad3f2f, "boot_cpu_data" },
	{ 0x63dc8f96, "__tracepoint_mmap_lock_released" },
	{ 0x8efcc8cd, "up_read" },
	{ 0xbed0ad9e, "mmput" },
	{ 0x1cf09ab5, "__put_task_struct_rcu_cb" },
	{ 0xb9fcd065, "call_rcu" },
	{ 0xf5711648, "__mmap_lock_do_trace_acquire_returned" },
	{ 0x987b0f38, "__mmap_lock_do_trace_start_locking" },
	{ 0x095159b2, "physical_mask" },
	{ 0x1bdf2bc8, "sme_me_mask" },
	{ 0xf296206e, "ptrs_per_p4d" },
	{ 0x4281043b, "pv_ops" },
	{ 0xd272d446, "BUG_func" },
	{ 0xbd03ed67, "page_offset_base" },
	{ 0x211f9d4e, "mem_section" },
	{ 0x7ec472ba, "__preempt_count" },
	{ 0xbd03ed67, "vmemmap_base" },
	{ 0x1fb0fe6c, "seq_printf" },
	{ 0xff0106da, "refcount_warn_saturate" },
	{ 0x987b0f38, "__mmap_lock_do_trace_released" },
	{ 0xd272d446, "__SCT__preempt_schedule" },
	{ 0x82fd7238, "__ubsan_handle_shift_out_of_bounds" },
	{ 0x771237db, "seq_read" },
	{ 0x501f3eec, "seq_lseek" },
	{ 0x5d8f1850, "single_release" },
	{ 0x40570a08, "param_ops_ulong" },
	{ 0x40570a08, "param_ops_int" },
	{ 0xd272d446, "__fentry__" },
	{ 0xc8104eec, "proc_create" },
	{ 0xd272d446, "__x86_return_thunk" },
	{ 0x10ca34d8, "module_layout" },
};

static const u32 ____version_ext_crcs[]
__used __section("__version_ext_crcs") = {
	0x09bf2a38,
	0x0fc9dafd,
	0xa6aed43d,
	0x63a4468d,
	0x95ae2e24,
	0x44a8e760,
	0x63dc8f96,
	0x8efcc8cd,
	0x63dc8f96,
	0xf296206e,
	0xb1ad3f2f,
	0x63dc8f96,
	0x8efcc8cd,
	0xbed0ad9e,
	0x1cf09ab5,
	0xb9fcd065,
	0xf5711648,
	0x987b0f38,
	0x095159b2,
	0x1bdf2bc8,
	0xf296206e,
	0x4281043b,
	0xd272d446,
	0xbd03ed67,
	0x211f9d4e,
	0x7ec472ba,
	0xbd03ed67,
	0x1fb0fe6c,
	0xff0106da,
	0x987b0f38,
	0xd272d446,
	0x82fd7238,
	0x771237db,
	0x501f3eec,
	0x5d8f1850,
	0x40570a08,
	0x40570a08,
	0xd272d446,
	0xc8104eec,
	0xd272d446,
	0x10ca34d8,
};
static const char ____version_ext_names[]
__used __section("__version_ext_names") =
	"single_open\0"
	"remove_proc_entry\0"
	"find_get_pid\0"
	"get_pid_task\0"
	"put_pid\0"
	"get_task_mm\0"
	"__tracepoint_mmap_lock_start_locking\0"
	"down_read\0"
	"__tracepoint_mmap_lock_acquire_returned\0"
	"pgdir_shift\0"
	"boot_cpu_data\0"
	"__tracepoint_mmap_lock_released\0"
	"up_read\0"
	"mmput\0"
	"__put_task_struct_rcu_cb\0"
	"call_rcu\0"
	"__mmap_lock_do_trace_acquire_returned\0"
	"__mmap_lock_do_trace_start_locking\0"
	"physical_mask\0"
	"sme_me_mask\0"
	"ptrs_per_p4d\0"
	"pv_ops\0"
	"BUG_func\0"
	"page_offset_base\0"
	"mem_section\0"
	"__preempt_count\0"
	"vmemmap_base\0"
	"seq_printf\0"
	"refcount_warn_saturate\0"
	"__mmap_lock_do_trace_released\0"
	"__SCT__preempt_schedule\0"
	"__ubsan_handle_shift_out_of_bounds\0"
	"seq_read\0"
	"seq_lseek\0"
	"single_release\0"
	"param_ops_ulong\0"
	"param_ops_int\0"
	"__fentry__\0"
	"proc_create\0"
	"__x86_return_thunk\0"
	"module_layout\0"
;

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "5516BDF31C33901EBB31A87");
