How to Run and Check the Project
Project Components:
1.	Assembler (ca2024asm/ca2024asm/asm.c) - Converts assembly (.asm) files to machine code
2.	Simulator (sim.c) - 4-core pipelined processor simulator with MESI cache coherence
3.	Test Programs - counter, mulserial, mulparallel
---
Step 1: Compile the Programs
Compile the Assembler:
cd ca2024asm/ca2024asm
gcc -o asm.exe asm.c project.c
Compile the Simulator:
gcc -o sim.exe sim.c
Step 2: Assemble Test Programs
# Example for counter program
cd counter
../ca2024asm/ca2024asm/asm.exe imem0.asm imem0.txt dmem0.txt
../ca2024asm/ca2024asm/asm.exe imem1.asm imem1.txt dmem1.txt
../ca2024asm/ca2024asm/asm.exe imem2.asm imem2.txt dmem2.txt
../ca2024asm/ca2024asm/asm.exe imem3.asm imem3.txt dmem3.txt
Assembler Usage:
asm program.asm imem.txt dmem.txt
Step 3: Run the Simulator
cd counter
../sim.exe imem0.txt imem1.txt imem2.txt imem3.txt \
           memin.txt memout.txt \
           regout0.txt regout1.txt regout2.txt regout3.txt \
           core0trace.txt core1trace.txt core2trace.txt core3.txt \
           bustrace.txt \
           dsram0.txt dsram1.txt dsram2.txt dsram3.txt \
           tsram0.txt tsram1.txt tsram2.txt tsram3.txt \
           stats0.txt stats1.txt stats2.txt stats3.txt
Or run with defaults (if input files are named as expected):
../sim.exe


or





