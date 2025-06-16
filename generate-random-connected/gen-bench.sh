#!/bin/bash

. .venv/bin/activate && python gen.py --arity 2 --height 18 --edge-factor 4 --skip-validation --num-procs 768 --seed 42
mv random-ar2-h18-e2097148-s42_524287_768 bench-connected-ar2-h18_524287_768
