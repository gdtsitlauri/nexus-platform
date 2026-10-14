# Polled UART output (NEY604): prints a message through the console/UART data register at 0x10000,
# waiting for the status register (0x10004, bit 0 = ready) before every byte. Runs unchanged on
# mips-sim (--mode pipeline --io-demo) and on the NEXUS SoC RTL. Exit code: number of bytes sent.
main:
  addu $s7, $ra, $zero        # keep the return address (putc uses jal)
  lui $s0, 1                  # $s0 = 0x10000, the I/O base
  addiu $s1, $zero, 0         # bytes sent
  addiu $a0, $zero, 72
  jal putc
  addiu $a0, $zero, 69
  jal putc
  addiu $a0, $zero, 76
  jal putc
  addiu $a0, $zero, 76
  jal putc
  addiu $a0, $zero, 79
  jal putc
  addiu $a0, $zero, 32
  jal putc
  addiu $a0, $zero, 78
  jal putc
  addiu $a0, $zero, 69
  jal putc
  addiu $a0, $zero, 88
  jal putc
  addiu $a0, $zero, 85
  jal putc
  addiu $a0, $zero, 83
  jal putc
  addiu $a0, $zero, 10
  jal putc
  addu $v0, $s1, $zero
  addu $ra, $s7, $zero
  jr $ra
putc:
  lw $t9, 4($s0)              # UART status
  beq $t9, $zero, putc        # busy: poll again
  sw $a0, 0($s0)              # transmit
  addiu $s1, $s1, 1
  jr $ra
