# Memory-bound thread: sums 200 words 64 bytes apart, so every load touches a new cache line (miss).
main:
  addiu $t0, $zero, 8192
  addiu $t1, $zero, 0
  addiu $t2, $zero, 200
  addiu $v0, $zero, 0
loop:
  lw    $t4, 0($t0)
  addu  $v0, $v0, $t4
  addiu $t0, $t0, 64
  addiu $t1, $t1, 1
  bne   $t1, $t2, loop
  addiu $v0, $v0, 7
  jr    $ra
