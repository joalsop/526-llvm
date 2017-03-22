# run proj526 pass on hello.bc enabling loop at line 10 to iterate 1, 2, or 3 times
opt -load ./lib/LLVMProj526.so -proj526 --loop_line=10 --iteration_counts=1,3,2 < hello.bc > /dev/null
