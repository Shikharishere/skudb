default: run

build:
    cmake -B build -S .
    cmake --build build

run: build
     ./build/skuDB sku.db

test: build
      cd build && ctest --output-on-failure

clean:
    rm -rf build/
