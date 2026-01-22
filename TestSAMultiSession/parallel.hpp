#pragma once

#include <vector>
#include <functional>
#include <mutex>
#include <algorithm>
#include <execution>


/** Helper functions for parallel execution of test cases
 */
namespace parallel {

  /** A parallel executor to run all the given functions in parallel while also allowing doctest assertions
   *  inside the executed functions. The thrown exceptions will be rethrown in the main thread (at most one).
   */
  inline void run(std::vector<std::function<void()>>&& functions) {
    std::exception_ptr exception_ptr = nullptr;
    std::mutex mutex;

    // By using for_each() we will use up to hardware_concurrency number of threads, which will be enough for our test cases.
    // Just be aware that passing more functors than that will not run them in parallel anymore.
    std::for_each(std::execution::par_unseq, functions.begin(), functions.end(), [&](std::function<void()>& functor) {
      try {
        functor();
      } catch (...) {
        // make sure there are no races when dealing with the exception ptr
        std::lock_guard<std::mutex> lock(mutex);

        // set the exception pointer in case of an exception - only store the first one
        // as the second may be caused by the error in the first failing thread. We don't want to overwrite it
        if (!exception_ptr) {
          exception_ptr = std::current_exception();
        }
      }
    });

    if (exception_ptr) {
      std::rethrow_exception(exception_ptr);
    }
  }

  /** A custom version of parallel for_each accepting a range and supporting assertions. Exceptions are rethrown in the main thread.
   */
  template<typename Range, typename Fn>
  void for_each(Range&& rng, Fn&& functor) {
    std::exception_ptr exception_ptr = nullptr;
    std::mutex mutex;


    std::for_each(std::execution::par_unseq, rng.begin(), rng.end(), [&](auto&& element) {
      try {
        functor(std::forward<decltype(element)>(element));
      } catch (...) {
        // make sure there are no races when dealing with the exception ptr
        std::lock_guard<std::mutex> lock(mutex);

        // set the exception pointer in case of an exception - only store the first one
        // as the second may be caused by the error in the first failing thread. We don't want to overwrite it
        if (!exception_ptr) {
          exception_ptr = std::current_exception();
        }
      }
    });

    if (exception_ptr) {
      std::rethrow_exception(exception_ptr);
    }
  }


  /** A helper class to delay the run of a thread until all expected threads arrived at the sync point and called wait on it.
   */
  class sync_point {
  public:
    sync_point(int nThreads) : nThreads(nThreads) {}

    void wait() {
      std::unique_lock<std::mutex> lock(mtx);
      if (--nThreads == 0) {
        lock.unlock();
        signal.notify_all();
      } else {
        // Wait for all threads to arrive at the sync point (wait for at most 3 seconds to not block for all eternity)
        if (!signal.wait_for(lock, std::chrono::seconds(3), [=]() { return nThreads == 0; })) {
          throw std::exception("Waiting for sync_point timed out!");
        }
      }
    }

  private:
    std::condition_variable signal;
    std::mutex mtx;
    int nThreads;
  };

}