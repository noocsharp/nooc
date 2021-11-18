write = 1
stdout = 0
exit = 60
len1 = 11
len2 = 6
syscall(write, stdout, "hello world", len1)
syscall(write, stdout, " world", len2)
syscall(exit, 0)
