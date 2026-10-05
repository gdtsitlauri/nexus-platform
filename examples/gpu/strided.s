# Thread t writes word 32*t: every lane hits its own 128-byte segment (no coalescing).
main:
kernel:
  sll $t0, $a0, 7
  addiu $t1, $a0, 100
  sw $t1, 4096($t0)
  lw $v0, 4096($t0)
  jr $ra
