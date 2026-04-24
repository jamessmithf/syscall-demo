CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2

# Розташування бінарників
BIN1 := task1_proc_mem/target
BIN2 := task1_proc_mem/monitor
BIN3 := task2_kbd_hook/kbd_hook
BIN4 := task2_kbd_hook/inject_events

.PHONY: all clean

all: $(BIN1) $(BIN2) $(BIN3) $(BIN4)

task1_proc_mem/target: task1_proc_mem/target.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

task1_proc_mem/monitor: task1_proc_mem/monitor.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

task2_kbd_hook/kbd_hook: task2_kbd_hook/kbd_hook.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

task2_kbd_hook/inject_events: task2_kbd_hook/inject_events.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(BIN1) $(BIN2) $(BIN3) $(BIN4)
