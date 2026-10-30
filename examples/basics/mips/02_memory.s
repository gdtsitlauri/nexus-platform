# Store a word, load it again, add one. Exit value: 13.
main:
  addiu $t0, $zero, 12
  sw    $t0, 0($zero)       # memory word at byte address 0 = 12
  lw    $t1, 0($zero)       # t1 = 12
  addiu $v0, $t1, 1
  jr    $ra
