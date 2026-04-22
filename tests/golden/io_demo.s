main:
  lui $t0, 1
  addiu $t1, $zero, 65
  sw $t1, 0($t0)
  addiu $v0, $zero, 65
  jr $ra
