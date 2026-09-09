BUILD_DIR := build/linux
OBJ_DIR := $(BUILD_DIR)/obj

CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Werror -Isrc
LDFLAGS :=

COMMON_SOURCES := $(shell find src -name '*.cpp' \
	! -path 'src/engine/platform/win32/*' \
	! -path 'src/engine/platform/linux/*' \
	! -path 'src/editor/*' \
	! -path 'src/tools/*' \
	! -name 'game.cpp' -print)
COMMON_OBJECTS := $(COMMON_SOURCES:src/%.cpp=$(OBJ_DIR)/common/%.o)
EDITOR_SOURCES := $(shell find src/editor -name '*.cpp' -print)
EDITOR_OBJECTS := $(EDITOR_SOURCES:src/editor/%.cpp=$(OBJ_DIR)/editor/%.o)
GAME_OBJECT := $(OBJ_DIR)/game.o
RUNNER_OBJECT := $(OBJ_DIR)/tools/playtest_runner.o
CONTENT_CHECK_OBJECT := $(OBJ_DIR)/tools/content_check.o
MAP_COMPILE_OBJECT := $(OBJ_DIR)/tools/map_compile.o
WORLD_COMPILE_OBJECT := $(OBJ_DIR)/tools/world_compile.o
TEST_OBJECT := $(OBJ_DIR)/tests/test_main.o
LINUX_OBJECTS := $(patsubst src/%.cpp,$(OBJ_DIR)/linux/%.o,$(shell find src/engine/platform/linux -name '*.cpp' -print))

ALL_OBJECTS := $(COMMON_OBJECTS) $(EDITOR_OBJECTS) $(GAME_OBJECT) $(RUNNER_OBJECT) $(CONTENT_CHECK_OBJECT) $(MAP_COMPILE_OBJECT) $(WORLD_COMPILE_OBJECT) $(TEST_OBJECT) $(LINUX_OBJECTS)
DEP_FILES := $(ALL_OBJECTS:.o=.d)

.PHONY: all build tests playtest game content_check map_compile world_compile clean

all: tests playtest game content_check map_compile world_compile
build: all

tests: $(BUILD_DIR)/tests
playtest: $(BUILD_DIR)/playtest_runner
game: $(BUILD_DIR)/game
content_check: $(BUILD_DIR)/content_check
map_compile: $(BUILD_DIR)/map_compile
world_compile: $(BUILD_DIR)/world_compile

$(BUILD_DIR)/tests: $(COMMON_OBJECTS) $(EDITOR_OBJECTS) $(TEST_OBJECT)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/playtest_runner: $(COMMON_OBJECTS) $(EDITOR_OBJECTS) $(GAME_OBJECT) $(RUNNER_OBJECT)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/game: $(COMMON_OBJECTS) $(GAME_OBJECT) $(LINUX_OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -lX11 -lpng -o $@

$(BUILD_DIR)/content_check: $(COMMON_OBJECTS) $(CONTENT_CHECK_OBJECT)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/map_compile: $(COMMON_OBJECTS) $(MAP_COMPILE_OBJECT)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/world_compile: $(COMMON_OBJECTS) $(WORLD_COMPILE_OBJECT)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(OBJ_DIR)/common/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR)/editor/%.o: src/editor/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR)/game.o: src/game/game.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR)/tools/playtest_runner.o: src/tools/playtest_runner.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR)/tools/content_check.o: src/tools/content_check.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR)/tools/map_compile.o: src/tools/map_compile.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR)/tools/world_compile.o: src/tools/world_compile.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR)/tests/test_main.o: tests/test_main.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR)/linux/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(BUILD_DIR)/tests $(BUILD_DIR)/playtest_runner $(BUILD_DIR)/game $(BUILD_DIR)/content_check $(BUILD_DIR)/map_compile $(BUILD_DIR)/world_compile

-include $(DEP_FILES)
