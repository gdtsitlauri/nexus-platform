main:
  j core0
core0:
  addiu $t0, $zero, 0
  addiu $t1, $zero, 9
  sw $t1, 0($t0)
  lui $t2, 2
  addiu $t3, $zero, 1
  sw $t3, 8($t2)
  lw $v0, 0($t0)
  jr $ra
core1:
  addiu $t0, $zero, 0
  lw $t4, 0($t0)
  lui $t2, 2
  addiu $t3, $zero, 1
  sw $t3, 8($t2)
  jr $ra
core2:
  addiu $t0, $zero, 0
  lw $t4, 0($t0)
  lui $t2, 2
  addiu $t3, $zero, 1
  sw $t3, 8($t2)
  jr $ra
core3:
  addiu $t0, $zero, 0
  lw $t4, 0($t0)
  lui $t2, 2
  addiu $t3, $zero, 1
  sw $t3, 8($t2)
  jr $ra
