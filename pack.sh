#!/bin/bash

# pack_solution.sh
# Packs Makefile from current directory and everything from ./src
# into solution.zip, which contains a single top-level folder specified by the argument.
# Uses a fixed staging directory "./build_pack_stage" which is NOT automatically deleted.
# Usage: ./pack_solution.sh <solution_folder_name_inside_zip>

# --- Configuration ---
SCRIPT_NAME=$(basename "$0")
ZIP_FILENAME="solution.zip"
# --- HARD-CODED STAGING DIRECTORY ---
# This directory will be created in the current working directory.
# It will NOT be automatically deleted by this script.
# You are responsible for cleaning it up if desired (e.g., rm -rf ./build_pack_stage)
STAGING_AREA_ROOT="./build_pack_stage"

# --- Argument Check ---
if [ -z "$1" ]; then
    echo "Usage: $SCRIPT_NAME <solution_folder_name_inside_zip>"
    echo "Example: $SCRIPT_NAME MyProject"
    exit 1
fi
SOLUTION_FOLDER_NAME="$1"

# --- Pre-checks for Output Zip ---
if [ -f "$ZIP_FILENAME" ]; then
    echo "Info: '$ZIP_FILENAME' already exists in the current directory. It will be overwritten."
    rm -f "$ZIP_FILENAME" # Remove existing zip to avoid appending or errors
fi

# --- Prepare Staging Directory ---
echo "Using staging area: $STAGING_AREA_ROOT"
# Remove the specific staging area if it exists from a previous run to ensure a clean state
if [ -d "$STAGING_AREA_ROOT" ]; then
    echo "Info: Previous staging area '$STAGING_AREA_ROOT' found. Removing it for a clean pack."
    rm -rf "./build_pack_stage" # This rm -rf is on a hard-coded, known path
    if [ $? -ne 0 ]; then
        echo "Error: Failed to remove previous staging area '$STAGING_AREA_ROOT'."
        exit 1
    fi
fi

# Create the root of the staging area
mkdir -p "$STAGING_AREA_ROOT"
if [ ! -d "$STAGING_AREA_ROOT" ]; then
    echo "Error: Failed to create staging area root: $STAGING_AREA_ROOT"
    exit 1
fi

# Create the target folder inside the staging directory
TARGET_CONTENT_DIR="$STAGING_AREA_ROOT/$SOLUTION_FOLDER_NAME"
mkdir -p "$TARGET_CONTENT_DIR"
if [ ! -d "$TARGET_CONTENT_DIR" ]; then
    echo "Error: Failed to create target content directory in staging area: $TARGET_CONTENT_DIR"
    # No explicit cleanup needed here as we don't rm -rf $STAGING_AREA_ROOT on general exit
	rm -rf "./build_pack_stage"
    exit 1
fi

# Copy Makefile
if [ -f "Makefile" ]; then
    cp "Makefile" "$TARGET_CONTENT_DIR/"
    echo "Copied Makefile to staging area."
else
    echo "Info: Makefile not found in the current directory. Skipping."
fi

# Copy raport
if [ -f "raport.pdf" ]; then
    cp "raport.pdf" "$TARGET_CONTENT_DIR/"
    echo "Copied Makefile to staging area."
else
    echo "Info: Makefile not found in the current directory. Skipping."
fi

# Copy src directory contents (if src exists and is not empty)
if [ -d "src" ]; then
    if [ -n "$(ls -A src)" ]; then
        cp -r "src" "$TARGET_CONTENT_DIR/"
        echo "Copied src/ directory to staging area."
    else
        echo "Info: src/ directory exists but appears empty. Skipping copy of src/."
    fi
else
    echo "Info: src/ directory not found. Skipping."
fi

# --- Create the Zip File ---
echo "Creating zip file: $ZIP_FILENAME"
# We change directory into STAGING_AREA_ROOT so that the paths inside the zip
# start with SOLUTION_FOLDER_NAME rather than the full path to STAGING_AREA_ROOT.
(
  cd "$STAGING_AREA_ROOT" || { echo "Error: Failed to cd into staging directory '$STAGING_AREA_ROOT'"; exit 1; }
  # zip -r target_zip_file source_folder_to_zip
  # The target_zip_file path is relative to the current dir (STAGING_AREA_ROOT),
  # so "../$ZIP_FILENAME" puts it in the original script execution directory.
  zip -r "../$ZIP_FILENAME" "$SOLUTION_FOLDER_NAME"
)
ZIP_EXIT_STATUS=$?

if [ $ZIP_EXIT_STATUS -ne 0 ]; then
    echo "Error: zip command failed with status $ZIP_EXIT_STATUS."
    echo "Staging area '$STAGING_AREA_ROOT' has been left for inspection."
    exit 1
fi

if [ -f "$ZIP_FILENAME" ]; then
    echo "Successfully created '$ZIP_FILENAME' containing '$SOLUTION_FOLDER_NAME/'."
else
    echo "Error: Failed to create '$ZIP_FILENAME'. Zip command might have silently failed."
    echo "Staging area '$STAGING_AREA_ROOT' has been left for inspection."
    exit 1
fi

echo "Packing complete."
rm -rf "./build_pack_stage"

exit 0