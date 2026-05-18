savedcmd_kprobe_cow.mod := printf '%s\n'   kprobe_cow.o | awk '!x[$$0]++ { print("./"$$0) }' > kprobe_cow.mod
