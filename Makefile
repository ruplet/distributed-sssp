SOLUTION_ZIP := solution.zip
# IMPORTANT: This MUST be the name of the single folder your packing script puts inside solution.zip.
# This is the folder that will be removed from testing_env before unzipping the new one.
TESTING_ENV_DIR := testing_env
TEST_SCRIPT := run_tests.py # Assumed to be at the root of LOGIN69 when unzipped

ALL : sssp_okeanos

.PHONY: test clean_test_env local

quota:
	lfs quota -uh $$USER /lu/tetyda/home/

watch:
	tail -f testing_env/output.txt

squeue:
	squeue --me --iterate 1

scancel:
	scancel -u $$USER

proc:
	ps -U $$USER

sssp_okeanos: src/main.cpp src/parse_data.cpp
	CC -std=c++17 -O3 -Wextra -Wpedantic -Wshadow -Wall -Werror $^ -o sssp -lm -Wno-sign-compare

local: src/main.cpp src/parse_data.cpp
	mpic++ -std=c++17 -O3 -Wextra -Wpedantic -Wshadow -Wall -Werror $^ -o sssp -lm -Wno-sign-compare

unit_test: src/unit_tests.cpp src/common.hpp src/block_dist.hpp
	mpic++ -std=c++17 -g -Wall -Werror src/unit_tests.cpp -o $@ -lm -fsanitize=undefined,address -fno-omit-frame-pointer

test-okeanos: $(SOLUTION_ZIP)
	@echo "--- Testing Solution ---"
	@echo "Calculating SHA256 sum of $(SOLUTION_ZIP)..."
	sha256sum $(SOLUTION_ZIP)

	@echo "Cleaning up previous solution artifacts in $(TESTING_ENV_DIR)/..."
	# Remove the old zip file from the testing environment, if it's there
	rm -f "$(TESTING_ENV_DIR)/$(SOLUTION_ZIP)"
	# Remove the old extracted solution folder from the testing environment, if it's there
	if [ -d "$(TESTING_ENV_DIR)/LOGIN69" ]; then \
		echo "Removing old solution folder: $(TESTING_ENV_DIR)/LOGIN69"; \
		rm -rf "$(TESTING_ENV_DIR)/LOGIN69"; \
	fi
	# Remove the old artifacts
	rm -f "$(TESTING_ENV_DIR)/core";
	rm -f "$(TESTING_ENV_DIR)/debug_log_*.txt";
	rm -f "$(TESTING_ENV_DIR)/outputs/*";
	rm -f "$(TESTING_ENV_DIR)/output.txt";

	@echo "Copying $(SOLUTION_ZIP) to $(TESTING_ENV_DIR)/..."
	cp $(SOLUTION_ZIP) $(TESTING_ENV_DIR)/

	@echo "Unzipping $(SOLUTION_ZIP) in $(TESTING_ENV_DIR)/..."
	# -q quiet
	# -d $(TESTING_ENV_DIR) extracts files into this directory
	unzip -q $(TESTING_ENV_DIR)/$(SOLUTION_ZIP) -d $(TESTING_ENV_DIR)

	setenv TESTMAX 70000 ; cd $(TESTING_ENV_DIR) && \
		sbatch sbatch_run_tests.sh && \
		echo "Waiting for edge splitter (PID=$$SPLITTER_PID) to finish..."

	@echo "--- Testing job submitted ---"


test-big: $(SOLUTION_ZIP)
	@echo "--- Testing Solution ---"
	@echo "Calculating SHA256 sum of $(SOLUTION_ZIP)..."
	sha256sum $(SOLUTION_ZIP)

	@echo "Cleaning up previous solution artifacts in $(TESTING_ENV_DIR)/..."
	# Remove the old zip file from the testing environment, if it's there
	rm -f "$(TESTING_ENV_DIR)/$(SOLUTION_ZIP)"
	# Remove the old extracted solution folder from the testing environment, if it's there
	if [ -d "$(TESTING_ENV_DIR)/LOGIN69" ]; then \
		echo "Removing old solution folder: $(TESTING_ENV_DIR)/LOGIN69"; \
		rm -rf "$(TESTING_ENV_DIR)/LOGIN69"; \
	fi
	# Remove the old artifacts
	rm -f "$(TESTING_ENV_DIR)/core";
	rm -f "$(TESTING_ENV_DIR)/debug_log_*.txt";
	rm -f "$(TESTING_ENV_DIR)/outputs/*";
	rm -f "$(TESTING_ENV_DIR)/output.txt";

	@echo "Copying $(SOLUTION_ZIP) to $(TESTING_ENV_DIR)/..."
	cp $(SOLUTION_ZIP) $(TESTING_ENV_DIR)/

	@echo "Unzipping $(SOLUTION_ZIP) in $(TESTING_ENV_DIR)/..."
	# -q quiet
	# -d $(TESTING_ENV_DIR) extracts files into this directory
	unzip -q $(TESTING_ENV_DIR)/$(SOLUTION_ZIP) -d $(TESTING_ENV_DIR)

	cd $(TESTING_ENV_DIR) && \
		sbatch sbatch_run_tests_big.sh
	# && \ echo "Waiting for edge splitter (PID=$$SPLITTER_PID) to finish..."

	@echo "--- Testing job submitted ---"

graph500: $(SOLUTION_ZIP)
	@echo "--- Testing Solution ---"
	@echo "Calculating SHA256 sum of $(SOLUTION_ZIP)..."
	sha256sum $(SOLUTION_ZIP)

	@echo "Cleaning up previous solution artifacts in $(TESTING_ENV_DIR)/..."
	# Remove the old zip file from the testing environment, if it's there
	rm -f "$(TESTING_ENV_DIR)/$(SOLUTION_ZIP)"
	# Remove the old extracted solution folder from the testing environment, if it's there
	if [ -d "$(TESTING_ENV_DIR)/LOGIN69" ]; then \
		echo "Removing old solution folder: $(TESTING_ENV_DIR)/LOGIN69"; \
		rm -rf "$(TESTING_ENV_DIR)/LOGIN69"; \
	fi
	# Remove the old artifacts
	rm -f "$(TESTING_ENV_DIR)/core";
	rm -f "$(TESTING_ENV_DIR)/debug_log_*.txt";
	rm -f "$(TESTING_ENV_DIR)/outputs/*";
	rm -f "$(TESTING_ENV_DIR)/output.txt";

	@echo "Copying $(SOLUTION_ZIP) to $(TESTING_ENV_DIR)/..."
	cp $(SOLUTION_ZIP) $(TESTING_ENV_DIR)/

	@echo "Unzipping $(SOLUTION_ZIP) in $(TESTING_ENV_DIR)/..."
	# -q quiet
	# -d $(TESTING_ENV_DIR) extracts files into this directory
	unzip -q $(TESTING_ENV_DIR)/$(SOLUTION_ZIP) -d $(TESTING_ENV_DIR)

	cd $(TESTING_ENV_DIR) && \
		sbatch sbatch_run_graph500.sh
	# && \ echo "Waiting for edge splitter (PID=$$SPLITTER_PID) to finish..."

	@echo "--- Testing job submitted ---"

solution.zip: pack.sh $(shell find src -type f) Makefile
	bash pack.sh LOGIN69