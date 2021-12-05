exit i64 = 60
write i64 = 1
loop {
	syscall(write, 0, "hello", 5)
}
syscall(exit, 0)
