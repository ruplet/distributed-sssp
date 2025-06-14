python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n3_ef1/ 3 1 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n3_ef1/ 3 2 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n3_ef1/ 3 3 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n3_ef1/ 3 4 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n3_ef1/ 3 8 . reuse

python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 5 4 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 6 4 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 7 4 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 8 4 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 9 4 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 10 4 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 11 4 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 12 4 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 13 4 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 14 4 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 15 4 . reuse
python3 graph500_convert_and_split.py ~/graph500/src/out_a5700_bc1900_d10000_s2_3_n5_ef16/ 16 4 . reuse



bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-12_4096_4'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-16_65536_4'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-3_8_4'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-7_128_4'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-13_8192_4'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-3_8_1'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-3_8_8'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-8_256_4'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-10_1024_4'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-14_16384_4'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-3_8_2'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-5_32_4'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-9_512_1'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-11_2048_4'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-15_32768_4'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-3_8_3'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-6_64_4'
bash -c '. ../generate-random-connected/.venv/bin/activate && python3 reference-dijkstra.py graph500-scale-9_512_4'

