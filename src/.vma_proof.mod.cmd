savedcmd_vma_proof.mod := printf '%s\n'   vma_proof.o | awk '!x[$$0]++ { print("./"$$0) }' > vma_proof.mod
