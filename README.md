# Concurrency\_in\_Cpp

Comparing the performance of threads and coroutines in C++

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

## Git Installation
Install [Git](https://git-scm.com/downloads).

- After installation, clone the repository
```
git clone https://github.com/RaiyanJahangir/Concurrency_in_Cpp.git
```
- Enter the cloned directory
```
cd Concurrency_in_Cpp
```

## Classic fixed pool (fixed threads + Global Queue)
Initial implementation of a classic fixed pool with a fixed number of threads and a global queue. We did a simple test by using 4 threads to handle 40 tasks (simple print statement) 
to make the initial implementation work.

Build:
```
g++ -O2 -std=c++20 -pthread test_threads.cpp thread_pool.cpp -o pool_test
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
- thread_pool.cpp
- test_elastic_pool.cpp

Example build:
```
g++ -std=c++20 -O2 -pthread thread_pool.cpp test_elastic_pool.cpp -o elastic_test
```
Run:
```
./elastic_test
```

## Advanced Elastic Pool (Dynamic Threads + Per-thread Queues + Stealing)

Added an advanced elastic pool that combines:
- dynamic threads (`min_threads` to `max_threads`)
- per-thread local deques
- work stealing between threads

Build:
```
g++ -std=c++20 -O2 -pthread thread_pool.cpp test_advanced_elastic_pool.cpp -o adv_elastic_test
```
Run:
```
./adv_elastic_test
```

## Run the Matrix Multiplication Benchmark "matrix_mul_bench.cpp"

```
g++ -O3 -std=c++20 -pthread matrix_mul_bench.cpp \
  thread_pool.cpp \
  -o matrix_mul_bench
```
Run the benchmark with one of the thread pool
```
./matrix_mul_bench classic 1024 64 8 1 3
./matrix_mul_bench ws      1024 64 8 1 3
./matrix_mul_bench elastic 1024 64 8 1 3
./matrix_mul_bench advws   1024 64 8 1 3
```
- 1st arg: number of the matrix dimension (N)
- 2nd arg: block size (BS)
- 3rd arg: number of threads
- 4th arg: number of warmup runs (not timed)
- 5th arg: number of timed runs (best and average reported)



## To start and run an experiment on CloudLab:

Go to "Start an Experment" at the top left drop-down list
1. Select a Profile (can just use the default profile)
2. Parameterize - Number of Nodes (1 is ok), Select OS image (Newest Ubuntu), Optional physical node type (check Resource Availability to see which server is available)
3. Finalize - Select the green cluster
4. Schedule - Choose the date and time to get the server, and how many hours we can have the server

Connect to the server through the shell using ssh commend given by the CloudLab once the experiment time starts

Clone the repo and run the test on the server
