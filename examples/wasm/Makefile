WASM_FLAGS = --target=wasm32 \
	-O3 \
	-flto \
	-nostdlib \
	-Wl,--no-entry \
	 -Wl,--export-all \
	-Wl,--lto-O3 \
	-DNDEBUG

grayskull.wasm: grayskull.c ../../grayskull.h
	$(CC) $(WASM_FLAGS) grayskull.c  -o grayskull.wasm
