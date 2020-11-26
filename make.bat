C:\LLVM10\bin\clang.exe -S -emit-llvm .\external\lib.cpp
C:\LLVM10\bin\llvm-link.exe .\lib.ll %1 -o test.bc
C:\LLVM10\bin\lli.exe test.bc
C:\LLVM10\bin\llc.exe -filetype=obj test.bc
C:\LLVM10\bin\clang.exe .\test.obj -o test.exe