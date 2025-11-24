	# Core 1: token-based round-robin counter increment
	add $r5, $zero, $imm, 1		# core id = 1
	add $r8, $zero, $imm, 128	# iterations remaining
	add $r3, $zero, $imm, 0		# base address 0
	add $r4, $zero, $imm, 1		# token address offset
	add $r13, $zero, $imm, loop	# loop start PC
loop:
	lw  $r7, $r3, $r4, 0		# load token
	bne $r13, $r7, $r5, 0		# wait until token == core id
	add $zero, $zero, $zero, 0	# delay slot nop
	lw  $r6, $r3, $r3, 0		# load counter
	add $r6, $r6, $imm, 1		# counter++
	sw  $r6, $r3, $r3, 0		# store counter
	add $r7, $zero, $imm, 2		# next token = 2
	sw  $r7, $r3, $r4, 0		# store token
	add $r8, $r8, $imm, -1		# iterations--
	bgt $r13, $r8, $zero, 0		# loop if iterations > 0
	add $zero, $zero, $zero, 0	# delay slot nop
	halt $zero, $zero, $zero, 0
