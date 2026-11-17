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

TESTS      := $(filter $(BUILD_DIR)/test_%,$(APPS))

.PHONY: all clean test
.SECONDARY: $(COMMON_OBJ)
all: $(APPS)

# Chạy lần lượt mọi build/test_*.exe, dừng ở test đầu tiên thất bại (mã thoát khác 0).
test: $(TESTS)
	@for t in $(TESTS); do \
		echo "== $$t"; \
		"$$t" || { echo "== THAT BAI: $$t"; exit 1; }; \
	done; \
	echo "== Tat ca test dat"

$(BUILD_DIR)/%$(EXE): %.cpp $(COMMON_OBJ) $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I. $< $(COMMON_OBJ) -o $@

$(BUILD_DIR)/obj/%.o: %.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I. -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
