savedcmd_kprobe_fork.mod := printf '%s\n'   kprobe_fork.o | awk '!x[$$0]++ { print("./"$$0) }' > kprobe_fork.mod
