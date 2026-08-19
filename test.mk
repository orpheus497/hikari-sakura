.if "${ASAN}" == "YES"
all:
	@echo "ASAN YES"
.else
all:
	@echo "ASAN NO"
.endif
