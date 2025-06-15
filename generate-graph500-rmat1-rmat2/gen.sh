./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n3_ef1/ 3 1 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n3_ef1/ 3 2 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n3_ef1/ 3 3 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n3_ef1/ 3 4 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n3_ef1/ 3 8 . reuse

./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 5 4 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 5 5 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 5 6 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 5 7 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 5 8 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 5 9 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 5 10 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 5 11 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 5 12 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 5 13 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 5 14 . reuse
./split ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 5 15 . reuse

bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-3_8_1'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-3_8_2'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-3_8_3'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-3_8_4'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-3_8_8'



bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-5_32_4' . reuse
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-5_32_5' . reuse
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-5_32_6' . reuse
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-5_32_7' . reuse
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-5_32_8' . reuse
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-5_32_9' . reuse
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-5_32_10' . reuse
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-5_32_11' . reuse
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-5_32_12' . reuse
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-5_32_13' . reuse
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-5_32_14' . reuse
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-5_32_15' . reuse
