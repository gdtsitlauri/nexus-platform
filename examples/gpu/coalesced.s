# Thread t writes word t of a global array: a warp touches one 128-byte segment per request.
main:
kernel:
  sll $t0, $a0, 2
  addiu $t1, $a0, 100
  sw $t1, 4096($t0)
  lw $v0, 4096($t0)
  jr $ra
