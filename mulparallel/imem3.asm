	# Core 3: rows 12-15 of 16x16 matrix multiply
	add $r2,  $zero, $imm, 12		# i = start row 12
	add $r15, $zero, $imm, 16		# end row (exclusive)
	add $r3,  $zero, $imm, 0		# j = 0
	add $r4,  $zero, $imm, 0		# k = 0
	add $r5,  $zero, $imm, 0		# sum = 0
	add $r11, $zero, $imm, 0x100		# baseB
	add $r12, $zero, $imm, 16		# N = 16
	add $r13, $zero, $imm, 0x200		# baseC
	add $r8,  $zero, $imm, loop_i		# target: loop_i
	add $r9,  $zero, $imm, loop_j		# target: loop_j
	add $r10, $zero, $imm, loop_k		# target: loop_k
loop_i:
	add $r3,  $zero, $imm, 0		# j = 0
loop_j:
	add $r4,  $zero, $imm, 0		# k = 0
	add $r5,  $zero, $imm, 0		# sum = 0
loop_k:
	mul $r6,  $r2, $r12, 0			# r6 = i * 16
	add $r6,  $r6, $r4, 0			# r6 += k
	lw  $r14, $r6, $zero, 0			# r14 = A[i][k]
	mul $r7,  $r4, $r12, 0			# r7 = k * 16
	add $r7,  $r7, $r3, 0			# r7 += j
	add $r7,  $r7, $r11, 0			# r7 += baseB
	lw  $r6,  $r7, $zero, 0			# r6 = B[k][j]
	mul $r7,  $r14, $r6, 0			# r7 = A*B
	add $r5,  $r5, $r7, 0			# sum += r7
	add $r4,  $r4, $imm, 1			# k++
	blt $r10, $r4, $r12			# while k < 16
	add $zero,  $zero, $zero, 0			# delay slot nop
	mul $r6,  $r2, $r12, 0			# r6 = i * 16
	add $r6,  $r6, $r3, 0			# r6 += j
	add $r6,  $r6, $r13, 0			# r6 += baseC
	sw  $r5,  $r6, $zero, 0			# C[i][j] = sum
	add $r3,  $r3, $imm, 1			# j++
	blt $r9,  $r3, $r12			# while j < 16
	add $zero,  $zero, $zero, 0			# delay slot nop
	add $r2,  $r2, $imm, 1			# i++
	blt $r8,  $r2, $r15			# while i < end
	add $zero,  $zero, $zero, 0			# delay slot nop
	halt $zero, $zero, $zero, 0
