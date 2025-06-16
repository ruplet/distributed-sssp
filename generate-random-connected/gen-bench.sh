#!/bin/bash

. .venv/bin/activate && python gen.py --arity 2 --height 16 --ef 4 --skip-validation --num-procs 768 --seed 42
