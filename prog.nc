write i64 = 1
stdout i64 = 0
exit i64 = 60
len1 i64 = 11
hello str = "hello "
world str = "world"
len2 i64 = + 3 3
syscall(write, stdout, hello, len2)
syscall(write, stdout, world, 5)
syscall(exit, 0)
