/Library/Developer/CommandLineTools/usr/bin/c++ -I../include -isystem /Users/luma/Downloads/llvm10/include -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX11.1.sdk -mmacosx-version-min=11.0 -std=gnu++2a -S -emit-llvm external/lib.cpp
~/Downloads/llvm10/bin/llvm-link ./lib.ll $1 -o test.bc
~/Downloads/llvm10/bin/lli test.bc
~/Downloads/llvm10/bin/llc -filetype=obj test.bc
~/Downloads/llvm10/bin/clang ./test.obj -o test