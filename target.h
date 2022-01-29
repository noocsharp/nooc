struct target {
	uint32_t reserved;
	size_t (*emitsyscall)(struct data *const text, const uint8_t paramcount);
	size_t (*emitproc)(struct data *const text, const struct iproc *const proc);
};

extern const struct target x64_target;
