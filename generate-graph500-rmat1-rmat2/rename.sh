#!/bin/bash


for dir in "rmat1"/*/; do
  # Get the base name (strip trailing slash and path)
  base=$(basename "$dir")
  new_name="rmat1/rmat1-bench-$base"

  # Rename the directory
  mv "$dir" "$new_name"
done

for dir in "rmat2"/*/; do
  # Get the base name (strip trailing slash and path)
  base=$(basename "$dir")
  new_name="rmat2/rmat2-bench-$base"

  # Rename the directory
  mv "$dir" "$new_name"
done
