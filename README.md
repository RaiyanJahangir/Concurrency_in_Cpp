# Concurrency\_in\_Cpp

Academic Project for ECS 251: Operating Systems



Testing comment: Start

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
