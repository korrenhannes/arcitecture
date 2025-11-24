1.	Build both executables and verify paths

msbuild .\sim.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64 /p:OutDir=.\x64\Release\ /p:IntDir=.\x64\Release\sim_int
msbuild .\ca2024asm\ca2024asm.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64 
Get-Item .\x64\Release\sim.exe; Get-Item .\ca2024asm\x64\Release\ca2024asm.exe

2.	COUNTER test

0..3 | % { & .\ca2024asm\x64\Release\ca2024asm.exe "counter\imem$($_).asm" "counter\imem$($_).txt" "counter\dummy.txt" }
Copy-Item .\x64\Release\sim.exe .\counter\sim.exe -Force
Push-Location counter
.\sim.exe
Pop-Location
$first = Get-Content .\counter\memout.txt -TotalCount 1
if ($first -ne '00000200') { Write-Error 'COUNTER FAILED' } else { 'COUNTER OK' }


3.	SERIAL matrix test

0..3 | % { & .\ca2024asm\x64\Release\ca2024asm.exe "mulserial\imem$($_).asm" "mulserial\imem$($_).txt" "mulserial\dummy.txt" }
Copy-Item .\x64\Release\sim.exe .\mulserial\sim.exe -Force
Push-Location mulserial
.\sim.exe
Pop-Location
Get-Content .\mulserial\memout.txt | Select-Object -Last 20



4.	PARALLEL matrix test

0..3 | % { & .\ca2024asm\x64\Release\ca2024asm.exe "mulparallel\imem$($_).asm" "mulparallel\imem$($_).txt" "mulparallel\dummy.txt" }
Copy-Item .\x64\Release\sim.exe .\mulparallel\sim.exe -Force
Push-Location mulparallel
.\sim.exe
Pop-Location
Get-Content .\mulparallel\memout.txt | Select-Object -Last 20


5.	Validate required 28 files per folder

0..3 | % { & .\ca2024asm\x64\Release\ca2024asm.exe "mulparallel\imem$($_).asm" "mulparallel\imem$($_).txt" "mulparallel\dummy.txt" }
Copy-Item .\x64\Release\sim.exe .\mulparallel\sim.exe -Force
Push-Location mulparallel
.\sim.exe
Pop-Location
Get-Content .\mulparallel\memout.txt | Select-Object -Last 20

6.	Optional: explicit 27-arg run (counter) Push-Location counter;

'counter','mulserial','mulparallel' | % {
  $f = Get-ChildItem $_ | Where-Object { $_.Name -match '^(sim\.exe|imem[0-3]\.txt|memin\.txt|memout\.txt|regout[0-3]\.txt|core[0-3]trace\.txt|bustrace\.txt|dsram[0-3]\.txt|tsram[0-3]\.txt|stats[0-3]\.txt)$' }
  "{0}: {1} files" -f $_, $f.Count
  if ($f.Count -ne 28) { Write-Error "$_ MISSING FILES" }
}


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



