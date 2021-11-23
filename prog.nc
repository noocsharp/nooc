write = 1
stdout = 0
exit = 60
len1 = 11
len2 = 6
syscall(write, + + 0 1 + 0 1, "hello world", + 1 + 2 + 3 + 4 1)
syscall(write, stdout, " world", len2)
syscall(exit, 0)
