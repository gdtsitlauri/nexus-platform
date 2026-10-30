# Byte addresses 0 and 4 share a default 16-byte cache line; 16 is another line.
# Three reads, two first-touch misses, one spatial-locality hit. Exit: 0.
main:
  lw    $t0, 0($zero)
  lw    $t1, 4($zero)
  lw    $v0, 16($zero)
  jr    $ra
