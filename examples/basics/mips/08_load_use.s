# The addu needs the load result immediately: one load-use stall. Exit: 10.
main:
  addiu $t0, $zero, 5
  sw    $t0, 0($zero)
  lw    $t1, 0($zero)
  addu  $v0, $t1, $t1
  jr    $ra
