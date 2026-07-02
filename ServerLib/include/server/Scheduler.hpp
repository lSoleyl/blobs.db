#pragma once

#include <network/IOCompletionPort.hpp>

#include <thread>
#include <mutex>
#include <chrono>
#include <condition_variable>


namespace blobs::server {

/** This class as a thread, which will be used to schedule Scheduler::Tasks at 
 *  specified times to the IOCompletionPort this scheduler is bound to.
 *  The tasks will be converted into 
 */
class Scheduler {
public:
  Scheduler(network::IOCompletionPort& ioCompletionPort);
  ~Scheduler();
  
  class Task {
    public:
      virtual ~Task() {}

      /** Called when the task is executed by the server's main thread after
       *  being posted to the IOCompletionPort
       */
      virtual void Run() = 0;

      /** Called if the task is cancelled due the the scheduler thread being stopped.
       *  Attention: This handler will be called from inside the scheduler thread, NOT the server's main thread.
       */
      virtual void Cancelled() {};
  };

  void RunAt(std::chrono::high_resolution_clock::time_point timePoint, std::unique_ptr<Task> task);

  /** Overload of RunAt() for scheduling arbitrary functions to be run 
   */
  template<typename Fn>
  void RunAt(std::chrono::high_resolution_clock::time_point timePoint, Fn&& runFn) {
    class SimpleTask {
      public: 
        SimpleTask(Fn&& runFn) : runFn(std::forward<Fn>(runFn)) {}
        virtual void Run() { runFn(); }
      private:
        Fn runFn;
    };

    RunAt(timePoint, std::unique_ptr<Task>(new SimpleTask(std::forward<Fn>(runFn))));
  }

  
  void RunIn(std::chrono::milliseconds ms, std::unique_ptr<Task> task);

  template<typename Fn>
  void RunIn(std::chrono::milliseconds ms, Fn&& runFn) {
    RunAt(std::chrono::high_resolution_clock::now() + ms, std::forward<Fn>(runFn));
  }

private:
  class ScheduledTask;

  void SchedulerThread();

  /** Returns the time until the next task is due or nullopt if there are no tasks scheduled
   */
  std::optional<std::chrono::milliseconds> NextTaskDeadline() const;

  // not copyable
  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  network::IOCompletionPort& ioCompletionPort;
  std::thread schedulerThread;
  std::mutex schedulerMutex;

  /** Condition variable, which is used to notify the scheduler thread of either the schedule changing or 
   *  that the scheduler has been stopped.
   */
  std::condition_variable scheduleChanged; 
  
  
  std::list<ScheduledTask*> scheduledTasks;

  bool stopped;
};

}
