let exit i64 = 60
let write i64 = 1
let stream i64 = 1
loop {
	syscall(write, stream, "hello\n", 6)
	stream = 2
}
syscall(exit, 0)
