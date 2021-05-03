Compiling the Example
=====================

Native Compiler
---------------

```shell
cd example
cmake -S . -B cmake-build
cd cmake-build
make
```

Cross-compiling for aarch64
---------------------------

```shell
cd example
cmake -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -B cmake-build -S .
cd cmake-build
make
```