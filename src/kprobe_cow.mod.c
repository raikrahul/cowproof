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
	{ 0xd272d446, "__x86_return_thunk" },
	{ 0xfe05c121, "const_current_task" },
	{ 0x888b8f57, "strcmp" },
	{ 0x2de0a194, "unregister_kprobe" },
	{ 0xd272d446, "__fentry__" },
	{ 0xb6377019, "register_kprobe" },
	{ 0xe8213e80, "_printk" },
	{ 0x10ca34d8, "module_layout" },
};

static const u32 ____version_ext_crcs[]
__used __section("__version_ext_crcs") = {
	0xd272d446,
	0xfe05c121,
	0x888b8f57,
	0x2de0a194,
	0xd272d446,
	0xb6377019,
	0xe8213e80,
	0x10ca34d8,
};
static const char ____version_ext_names[]
__used __section("__version_ext_names") =
	"__x86_return_thunk\0"
	"const_current_task\0"
	"strcmp\0"
	"unregister_kprobe\0"
	"__fentry__\0"
	"register_kprobe\0"
	"_printk\0"
	"module_layout\0"
;

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "23DFA49FD52176664AD2827");
