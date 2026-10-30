# Always-taken branch defeats static-not-taken prediction. Exit must be 7.
# NEXUS has no architectural branch delay slot: the wrong-path 99 is discarded.
main:
  addiu $v0, $zero, 7
  beq   $zero, $zero, done
  addiu $v0, $zero, 99
done:
  jr    $ra
