SRC_DIR=src
TST_DIR=tst
BUILD_DIR=build
INSTALL_DIR=/usr/local

CC=gcc
CFLAGS=-W -Wall -Isrc -D_GNU_SOURCE

SRC=$(sort $(wildcard $(SRC_DIR)/*.c))
OBJ=$(notdir $(SRC:.c=.o))

SRC_TST=$(sort $(wildcard $(TST_DIR)/*.c))
BIN_TST=$(notdir $(SRC_TST:.c=))


all: lib


create_build_dir:
	mkdir -p $(BUILD_DIR)


%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -fpic -c -o $(BUILD_DIR)/$@ $<


lib: create_build_dir $(OBJ)
	$(CC) $(CFLAGS) -shared -o $(BUILD_DIR)/libthread.so $(foreach obj_file,$(OBJ),$(BUILD_DIR)/$(obj_file))
	for file in $(BIN_TST); do \
		$(CC) $(CFLAGS) -o $(BUILD_DIR)/$$file $(TST_DIR)/$${file}.c -L$(PWD)/$(BUILD_DIR) -Wl,-rpath=$(PWD)/$(BUILD_DIR) -lthread -lrt; \
	done


check: lib
	for file in $(BIN_TST); do \
		echo $$file 10 500:; \
		$(BUILD_DIR)/$$file 10 500; \
		echo; \
	done


valgrind: lib
	for file in $(BIN_TST); do \
		valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes $(BUILD_DIR)/$$file 10 500; \
	done


pthreads:
	mkdir -p $(BUILD_DIR)
	for file in $(BIN_TST); do \
		$(CC) $(CFLAGS) -o $(BUILD_DIR)/$${file}_pthread -DUSE_PTHREAD -pthread $(TST_DIR)/$${file}.c; \
	done


graphs: lib pthreads
	./plot.sh


install:
	cp $(BUILD_DIR)/libthread.so /usr/local/lib
	cp $(SRC_DIR)/*.h /usr/local/include


clean:
	@rm -rf $(BUILD_DIR)/*
