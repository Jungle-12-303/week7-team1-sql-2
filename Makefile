CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
TARGET := $(BUILD_DIR)/mini_sql
TEST_TARGET := $(BUILD_DIR)/test_runner

COMMON_SOURCES := \
	src/common.c \
	src/parser.c \
	src/storage.c \
	src/executor.c

APP_SOURCES := \
	$(COMMON_SOURCES) \
	src/main.c

TEST_SOURCES := \
	$(COMMON_SOURCES) \
	tests/test_runner.c

.PHONY: all test clean run

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(APP_SOURCES) include/common.h include/parser.h include/storage.h include/executor.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(APP_SOURCES) -o $(TARGET)

$(TEST_TARGET): $(TEST_SOURCES) include/common.h include/parser.h include/storage.h include/executor.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_SOURCES) -o $(TEST_TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

run: $(TARGET)
	./$(TARGET) examples/db examples/sql/demo_workflow.sql

clean:
	rm -rf $(BUILD_DIR)
