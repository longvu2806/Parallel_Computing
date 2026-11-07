# Mỗi file .cpp trong dot_product/, matmul/, tests/ là một chương trình: build/<tên>.exe.
# Đổi cờ khi thí nghiệm, ví dụ: make clean && make CXXFLAGS="-std=c++17 -O2 -fopenmp"

CXX       ?= g++
CXXFLAGS  ?= -std=c++17 -O3 -march=native -Wall -Wextra -fopenmp
BUILD_DIR ?= build

# g++ trên Windows luôn thêm đuôi .exe; khai báo để make không dịch lại mỗi lần
EXE := $(if $(filter Windows_NT,$(OS)),.exe,)

HEADERS    := $(wildcard common/*.hpp matmul/*.hpp)
COMMON_OBJ := $(patsubst %.cpp,$(BUILD_DIR)/obj/%.o,$(wildcard common/*.cpp))
APP_SRC    := $(wildcard dot_product/*.cpp matmul/*.cpp tests/*.cpp)
APPS       := $(addsuffix $(EXE),$(addprefix $(BUILD_DIR)/,$(basename $(notdir $(APP_SRC)))))

vpath %.cpp dot_product matmul tests

.PHONY: all clean
.SECONDARY: $(COMMON_OBJ)
all: $(APPS)

$(BUILD_DIR)/%$(EXE): %.cpp $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I. $< $(COMMON_OBJ) -o $@

$(BUILD_DIR)/obj/%.o: %.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I. -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
