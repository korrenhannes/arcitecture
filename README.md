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


or using vscode:

Build the simulator (from the project root):

cd "/Users/korrenhannes/Library/Mobile Documents/com~apple~CloudDocs/Documents/homework/arcitecture"
gcc -std=c99 -Wall -Wextra -O2 sim.c -o sim

Run against the provided example bundle (writes outputs to /tmp/verify):

mkdir -p /tmp/verify

./sim example_221125_win/imem0.txt example_221125_win/imem1.txt example_221125_win/imem2.txt example_221125_win/imem3.txt \
      example_221125_win/memin.txt /tmp/verify/memout.txt \
      /tmp/verify/regout0.txt /tmp/verify/regout1.txt /tmp/verify/regout2.txt /tmp/verify/regout3.txt \
      /tmp/verify/core0trace.txt /tmp/verify/core1trace.txt /tmp/verify/core2trace.txt /tmp/verify/core3trace.txt \
      /tmp/verify/bustrace.txt \
      /tmp/verify/dsram0.txt /tmp/verify/dsram1.txt /tmp/verify/dsram2.txt /tmp/verify/dsram3.txt \
      /tmp/verify/tsram0.txt /tmp/verify/tsram1.txt /tmp/verify/tsram2.txt /tmp/verify/tsram3.txt \
      /tmp/verify/stats0.txt /tmp/verify/stats1.txt /tmp/verify/stats2.txt /tmp/verify/stats3.txt
      
Run your test bundles (from the project root; adjust paths for mulserial/mulparallel similarly):

./sim counter/imem0.txt counter/imem1.txt counter/imem2.txt counter/imem3.txt \
      counter/memin.txt counter/memout.txt \
      counter/regout0.txt counter/regout1.txt counter/regout2.txt counter/regout3.txt \
      counter/core0trace.txt counter/core1trace.txt counter/core2trace.txt counter/core3trace.txt \
      counter/bustrace.txt \
      counter/dsram0.txt counter/dsram1.txt counter/dsram2.txt counter/dsram3.txt \
      counter/tsram0.txt counter/tsram1.txt counter/tsram2.txt counter/tsram3.txt \
      counter/stats0.txt counter/stats1.txt counter/stats2.txt counter/stats3.txt
      
(or simply cd counter && ./sim since the files are already there and named as defaults).



