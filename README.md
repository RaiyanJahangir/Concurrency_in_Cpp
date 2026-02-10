# Concurrency\_in\_Cpp

Academic Project for ECS 251: Operating Systems

## Classic fixed pool (fixed threads + Global Queue)
Initial implementation of a classic fixed pool with a fixed number of threads and a global queue. We did a simple test by using 4 threads to handle 40 tasks (simple print statement) 
to make the initial implementation work.

To build: g++ -O2 -std=c++20 -pthread test_threads.cpp thread_pool_classic_fixed_global.cpp -o pool_test
To run the test: ./pool_test

## Elastic Pool (Dynamic Threads + Global Queue)

Added an elastic thread pool that grows/shrinks between a minimum and maximum
thread count with an idle timeout.

Files:
- thread_pool_elastic.h
- thread_pool_elastic.cpp
- test_elastic_pool.cpp

Example build:
```
g++ -std=c++20 -O2 -pthread thread_pool_elastic.cpp test_elastic_pool.cpp -o elastic_test
```
