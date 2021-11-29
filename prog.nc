write i64 = 1
stdout i64 = 0
exit i64 = 60
len1 i64 = 11
len2 i64 = 6
syscall(write, stdout, "hello world", (+ 1 (+ 5 5)))
syscall(write, stdout, " world", len2)
syscall(exit, 0)
