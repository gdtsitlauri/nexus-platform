main:
  j core0
core0:
  addiu $t1, $zero, 1
  sw $t1, 0($zero)
  sw $t1, 4($zero)
  lw $v0, 0($zero)
  jr $ra
core1:
  addiu $t0, $zero, 4
wait:
  lw $t2, 0($t0)
  beq $t2, $zero, wait
  addiu $t0, $zero, 0
  lw $v0, 0($t0)
  jr $ra
