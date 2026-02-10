# Concurrency\_in\_Cpp

Comparing the performance of threads and co-routines

## C++20 Toolchain Install

Ubuntu (GCC/G++): https://documentation.ubuntu.com/ubuntu-for-developers/howto/gcc-setup/
```
sudo apt install gcc g++
```

macOS (LLVM/Clang via Homebrew): https://formulae.brew.sh/formula/llvm
```
brew install llvm
```

Windows (MSVC Build Tools): https://visualstudio.microsoft.com/downloads/
Download and install Build Tools for Visual Studio, then select the C++ workload during setup.

## Classic fixed pool (fixed threads + Global Queue)
Initial implementation of a classic fixed pool with a fixed number of threads and a global queue. We did a simple test by using 4 threads to handle 40 tasks (simple print statement) 
to make the initial implementation work.

Build:
```
g++ -O2 -std=c++20 -pthread test_threads.cpp thread_pool_classic_fixed_global.cpp -o pool_test
```
Run:
```
./pool_test
```

## Elastic Pool (Dynamic Threads + Global Queue)

Added an elastic thread pool that grows/shrinks between a minimum and a maximum
thread count with an idle timeout.

Files:
- thread_pool.h
- thread_pool_elastic.cpp
- test_elastic_pool.cpp

Example build:
```
g++ -std=c++20 -O2 -pthread thread_pool_elastic.cpp test_elastic_pool.cpp -o elastic_test
```
Run:
```
./elastic_test
```

## To start and run an experiment on CloudLab:

Go to "Start an Experment" at the top left drop-down list
1. Select a Profile (can just use the default profile)
2. Parameterize - Number of Nodes (1 is ok), Select OS image (Newest Ubuntu), Optional physical node type (check Resource Availability to see which server is available)
3. Finalize - Select the green cluster
4. Schedule - Choose the date and time to get the server, and how many hours we can have the server

Connect to the server through the shell using ssh commend given by the CloudLab once the experiment time starts

Clone the repo and run the test on the server
