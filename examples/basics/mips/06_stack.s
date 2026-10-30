# The same call, now preserving ra in a small stack frame. Exit value: 9.
main:
  addiu $sp, $sp, -8
  sw    $ra, 4($sp)
  addiu $a0, $zero, 8
  jal   increment
  lw    $ra, 4($sp)
  addiu $sp, $sp, 8
  jr    $ra
increment:
  addiu $v0, $a0, 1
  jr    $ra
