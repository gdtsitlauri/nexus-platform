main:
  j core0
core0:
  lui $t7, 2
  lw $t0, 12($t7)
  sw $t0, 64($zero)
  addiu $t1, $zero, 1
  sw $t1, 8($t7)
  lw $t2, 64($zero)
  lw $t3, 68($zero)
  addu $v0, $t2, $t3
  jr $ra
core1:
  lui $t7, 2
  lw $t0, 12($t7)
  sw $t0, 68($zero)
  addiu $t1, $zero, 1
  sw $t1, 8($t7)
  jr $ra
