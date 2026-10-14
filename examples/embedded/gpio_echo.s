# GPIO (NEY604, SoC only): reads the switches at 0x10304, writes their bit-wise complement to the LEDs at 0x10300,
# then counts the 1 bits of the switches in a loop and returns the count as the exit code.
main:
  lui $s0, 1
  lw $t0, 772($s0)            # switches (0x10304)
  addiu $t1, $zero, 255
  xor $t2, $t0, $t1           # complement of the 8 switch bits
  sw $t2, 768($s0)            # LEDs (0x10300)
  addiu $v0, $zero, 0
  addiu $t3, $zero, 8         # 8 bits to examine
loop:
  beq $t3, $zero, done
  addiu $t4, $zero, 1
  and $t5, $t0, $t4
  addu $v0, $v0, $t5
  addiu $t6, $zero, 1
  sltu $t7, $zero, $t0        # (shift right by one using division by 2)
  addiu $t8, $zero, 2
  div $t0, $t8
  mflo $t0
  addiu $t3, $t3, -1
  j loop
done:
  jr $ra
