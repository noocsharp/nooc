exit i64 = 60
write i64 = 1
loop {
	syscall(write, 0, "hello\n", 6)
}
syscall(exit, 0)
