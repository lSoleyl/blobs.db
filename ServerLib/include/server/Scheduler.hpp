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
      virtual void Cancelled() = 0;

      /** Static task creation helper for simple tasks with only a run function. The passed function will be run once
       *  task is executed. The SimpleTask and thus the function object are deleted afterwards.
       * 
       * @param runFn the function to execute in the server's main thread once the task is scheduled to run
       */
      template<typename Fn>
      static std::unique_ptr<Task> Create(Fn&& runFn) {
        class SimpleTask : public Task {
        public:
          SimpleTask(Fn&& runFn) : runFn(std::forward<Fn>(runFn)) {}
          virtual void Run() override { runFn(); }
        private:
          Fn runFn;
        };

        return std::unique_ptr<Task>(new SimpleTask(std::forward<Fn>(runFn)));
      }

      /** Static task creation helper for tasks with a cancel function that is run if the task is being deleted before
       *  it could run if the server is shut down before the task could be scheduled.
       * 
       * @param runFn the function to execute in the server's main thread once the task is scheduled to run
       * @param cancelFn the function to execute in the scheduler's thread if the task is prematurely cancelled
       */
      template<typename FnRun, typename FnCancel>
      static std::unique_ptr<Task> Create(FnRun&& runFn, FnCancel&& cancelFn) {
        class RegularTask : public Task {
        public:
          RegularTask(FnRun&& runFn, FnCancel&& cancelFn) : runFn(std::forward<FnRun>(runFn)), cancelFn(std::forward<FnCancel>(cancelFn)) {}
          virtual void Run() override { runFn(); }
          virtual void Cancelled() override { cancelFn(); }
        private:
          FnRun runFn;
          FnCancel cancelFn;
        };

        return std::unique_ptr<Task>(new RegularTask(std::forward<FnRun>(runFn), std::forward<FnCancel>(cancelFn));
      }
  };

  /** Runs the given task at the specified time point
   */
  void RunAt(std::chrono::high_resolution_clock::time_point timePoint, std::unique_ptr<Task> task);

  /** Runs the given task after the specified number of milliseconds
   */
  void RunIn(std::chrono::milliseconds ms, std::unique_ptr<Task> task);

  /** Overload of RunIn() with an arbitrary duration type
   */
  template<typename Rep, typename Period>
  void RunIn(std::chrono::duration<Rep, Period> duration, std::unique_ptr<Task> task) {
    RunAt(std::chrono::high_resolution_clock::now() + duration, std::move(task)));
  }

private:
  class ScheduledTask;

  /** The scheduler thread's main function
   */
  void SchedulerThreadMain();

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
