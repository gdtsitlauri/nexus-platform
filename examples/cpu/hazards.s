# Forwarding and a load-use stall (EY321). The branch is not taken: no flush.
# See examples/basics/mips/09_flush.s for a taken branch and wrong-path flush.
main:
  addiu $t0, $zero, 5      # t0 = 5
  addu  $t1, $t0, $t0      # needs t0 one cycle later: forwarding (EX -> EX), no stall
  sw    $t1, 0($sp)
  lw    $t2, 0($sp)        # load ...
  addu  $t3, $t2, $t2      # ... used at once: load-use hazard, 1 stall even with forwarding
  beq   $t3, $zero, skip   # not taken here
  addiu $v0, $t3, 0        # v0 = 20
skip:
  jr    $ra
