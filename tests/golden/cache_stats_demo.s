main:
  addiu $t0, $zero, 0
  addiu $t1, $zero, 5
  sw $t1, 0($t0)
  lw $t2, 0($t0)
  addu $v0, $t2, $zero
  jr $ra
