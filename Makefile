TARGET=
VERBOSE=
Q=@

all: build

build:
	$(Q)./fips make

clean:
	$(Q)./fips clean
	$(Q)rm -f .fips-gen.py
	$(Q)rm -f .fips-imports.cmake

eclipse:
	$(Q)./fips set config linux-eclipse-debug

server: build
	$(Q)./fips run server -- $(ARGS)

client: build
	$(Q)./fips run client -- $(ARGS)

shapetool: build
	$(Q)./fips run shapetool -- $(ARGS)

cubiquitytool: build
	$(Q)./fips run cubiquitytool -- $(ARGS)

debugcubiquitytool: build
	$(Q)./fips gdb cubiquitytool -- $(ARGS)

debugserver: build
	$(Q)./fips gdb server -- $(ARGS)

debugclient: build
	$(Q)./fips gdb client -- $(ARGS)

debugshapetool: build
	$(Q)./fips gdb shapetool -- $(ARGS)

tests: build
	$(Q)./fips run tests -- $(ARGS)

tests-list: build
	$(Q)./fips run tests -- --gtest_list_tests $(ARGS)

tests-filter: build
	$(Q)./fips run tests -- --gtest_filter=$(FILTER) $(ARGS)
