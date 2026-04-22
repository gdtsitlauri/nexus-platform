main:
  addiu $t0, $zero, 1
  beq $t0, $t0, taken
  addiu $v0, $zero, 0
taken:
  addiu $v0, $zero, 7
  jr $ra
