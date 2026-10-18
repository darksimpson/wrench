OPT ?= -O3
#OPT ?= -O0 -ggdb

PERF ?=

# the VM and its debug server stay plain C++98 so they build for any target,
# the host tools (debug client, CLI debugger, DAP and LSP servers) use C++17
C_VER ?= -std=c++98
HOST_C_VER ?= -std=c++17

# debugging support: server in the VM, client and protocol servers in the CLI
DEBUG ?= -DWRENCH_INCLUDE_DEBUG_CODE -DWRENCH_INCLUDE_DEBUG_CLIENT

PLATFORM ?= -DWRENCH_LINUX_FILE_IO -DWRENCH_LINUX_SERIAL -DWRENCH_LINUX_TCP

FLAGS ?= $(OPT) $(PERF) -iquote src -I. $(COMPACT) -MD $(DEBUG) $(PLATFORM)

# extra link flags, "make LDFLAGS=-static" gives a binary that runs on any
# distribution (used for the copy bundled in the VS Code extension)
LDFLAGS ?=

all: wrench

OBJDIR = objs_linux

-include $(OBJDIR)/*.d

CC = g++ -Wall -Werror $(FLAGS) $(C_VER) -c -o
HOSTCC = g++ -Wall -Werror $(FLAGS) $(HOST_C_VER) -c -o

CORE_OBJS = \
	$(OBJDIR)/cc.o \
	$(OBJDIR)/expression.o \
	$(OBJDIR)/link.o \
	$(OBJDIR)/optimizer.o \
	$(OBJDIR)/token.o \
	$(OBJDIR)/operations.o \
	$(OBJDIR)/operations_compact.o \
	$(OBJDIR)/operations_non_compact.o \
	$(OBJDIR)/index.o \
	$(OBJDIR)/gc.o \
	$(OBJDIR)/gc_object.o \
	$(OBJDIR)/vm.o \
	$(OBJDIR)/utils.o \
	$(OBJDIR)/serializer.o \
	$(OBJDIR)/debug.o \
	$(OBJDIR)/disassemble.o \
	$(OBJDIR)/debug_protocol.o \
	$(OBJDIR)/debug_server.o \
	$(OBJDIR)/debug_transport.o \
	$(OBJDIR)/scheduler.o \
	$(OBJDIR)/std.o \
	$(OBJDIR)/std_tcp.o \
	$(OBJDIR)/std_io.o \
	$(OBJDIR)/std_string.o \
	$(OBJDIR)/std_math.o \
	$(OBJDIR)/std_msg.o \
	$(OBJDIR)/std_sys.o \
	$(OBJDIR)/std_serialize.o \
	$(OBJDIR)/std_container.o \
	$(OBJDIR)/debug_lib.o \
	$(OBJDIR)/linux_comm.o \
	$(OBJDIR)/fastled_lib.o \

HOST_OBJS = \
	$(OBJDIR)/debug_client.o \
	$(OBJDIR)/debugger.o \
	$(OBJDIR)/wrench_debug_host.o \
	$(OBJDIR)/wrench_project.o \
	$(OBJDIR)/wrench_debug_eval.o \
	$(OBJDIR)/wrench_dap.o \
	$(OBJDIR)/wrench_lsp.o \
	$(OBJDIR)/json.o \
	$(OBJDIR)/json_stream.o \
	$(OBJDIR)/test_debugger.o \
	$(OBJDIR)/wrench_cli.o \

OBJS = $(CORE_OBJS) $(HOST_OBJS)

clean:
	-@rm -f $(OBJDIR)/*.o $(OBJDIR)/*.d
	-@rm -f wrench
	-@rm -f wrench_dev
	-@rm -f wrench_v
	-@rm -f test_wrench_cross_module_globals
	-@rm -rf $(DIST)

wrench: $(OBJS)
	g++ -o wrench $(OBJS) -pthread $(LDFLAGS)

valgrind: $(OBJS)
	g++ -o wrench_v $(OBJS) -pthread -ggdb

tests: test
tested: test

test: wrench
	g++ $(CORE_OBJS) -Wall -Werror $(FLAGS) -std=c++11 src/utils/test_wrench_cross_module_globals.cpp -DDOCTEST_CONFIG_IMPLEMENT_WITH_MAIN -o test_wrench_cross_module_globals
	./test_wrench_cross_module_globals
	./wrench t

# the single file build dist/wrench.[cpp|h], published by CI with every release
DIST = dist

.PHONY: dist release dist-check

dist: wrench
	mkdir -p $(DIST)
	./wrench release src $(DIST)

release: dist

# the single file build must compile on its own in every configuration users
# pick, and an example program must link and run against it
DIST_VARIANTS = \
	"" \
	"-DWRENCH_INCLUDE_DEBUG_CODE -DWRENCH_LINUX_TCP -DWRENCH_LINUX_SERIAL -DWRENCH_LINUX_FILE_IO" \
	"-DWRENCH_COMPACT" \
	"-DWRENCH_COMPACT -DWRENCH_REALLY_COMPACT -DWRENCH_INCLUDE_DEBUG_CODE" \
	"-DWRENCH_WITHOUT_COMPILER -DWRENCH_COMPACT -DWRENCH_REALLY_COMPACT" \
	"-DWRENCH_UNPROTECTED_STACK -DWRENCH_HANDLE_MALLOC_FAIL -DWRENCH_TIME_SLICES"

dist-check: dist
	@for v in $(DIST_VARIANTS); do \
		echo "single file build: [$$v]"; \
		g++ -std=c++98 -Wall -Werror -O1 -I$(DIST) $$v -c $(DIST)/wrench.cpp -o $(DIST)/check.o || exit 1; \
	done
	g++ -std=c++98 -Wall -Werror -O1 -I$(DIST) -DWRENCH_INCLUDE_DEBUG_CODE -DWRENCH_LINUX_TCP $(DIST)/wrench.cpp examples/basic.cpp -o $(DIST)/basic
	cd examples && ../$(DIST)/basic
	-@rm -f $(DIST)/check.o $(DIST)/basic

$(OBJDIR)/%.o: src/cc/%.cpp
	$(CC) $@ $<

$(OBJDIR)/%.o: src/vm/%.cpp
	$(CC) $@ $<

$(OBJDIR)/%.o: src/lib/%.cpp
	$(CC) $@ $<

$(OBJDIR)/debug.o: src/debug/debug.cpp
	$(CC) $@ $<

$(OBJDIR)/disassemble.o: src/debug/disassemble.cpp
	$(CC) $@ $<

$(OBJDIR)/debug_protocol.o: src/debug/debug_protocol.cpp
	$(CC) $@ $<

$(OBJDIR)/debug_server.o: src/debug/debug_server.cpp
	$(CC) $@ $<

$(OBJDIR)/debug_transport.o: src/debug/debug_transport.cpp
	$(CC) $@ $<

$(OBJDIR)/utils.o: src/utils/utils.cpp
	$(CC) $@ $<

$(OBJDIR)/serializer.o: src/utils/serializer.cpp
	$(CC) $@ $<

$(OBJDIR)/linux_comm.o: src/utils/linux_comm.cpp
	$(CC) $@ $<

$(OBJDIR)/debug_client.o: src/debug/debug_client.cpp
	$(HOSTCC) $@ $<

$(OBJDIR)/debugger.o: src/debug/debugger.cpp
	$(HOSTCC) $@ $<

$(OBJDIR)/%.o: src/utils/%.cpp
	$(HOSTCC) $@ $<
