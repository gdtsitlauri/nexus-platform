main:
  addiu $t0, $zero, 0
  addiu $t1, $zero, 21
  sw $t1, 0($t0)
  addiu $t1, $zero, 22
  sw $t1, 4($t0)
  lui $t2, 1
  addiu $t3, $zero, 0
  sw $t3, 512($t2)
  addiu $t3, $zero, 64
  sw $t3, 516($t2)
  addiu $t3, $zero, 2
  sw $t3, 520($t2)
  addiu $t3, $zero, 1
  sw $t3, 524($t2)
  addiu $t5, $zero, 1
wait:
  lw $t4, 528($t2)
  bne $t4, $t5, wait
  lw $v0, 64($zero)
  jr $ra
