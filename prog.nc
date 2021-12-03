exit i64 = 60
write i64 = 1
if > 1 0 {
	syscall(write, 0, "hello", 5)
} else {
	syscall(write, 0, "world", 5)
}
syscall(exit, 0)
