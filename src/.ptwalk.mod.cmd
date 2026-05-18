savedcmd_ptwalk.mod := printf '%s\n'   ptwalk.o | awk '!x[$$0]++ { print("./"$$0) }' > ptwalk.mod
