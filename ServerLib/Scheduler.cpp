#include "pch.hpp"
#include <server/Scheduler.hpp>


namespace blobs::server {

Scheduler::Scheduler(network::IOCompletionPort& ioCompletionPort) : ioCompletionPort(ioCompletionPort), stopped(false) {
  schedulerThread = std::thread([=]() { SchedulerThread(); });
}

Scheduler::~Scheduler() {
  {
    std::unique_lock lock(schedulerMutex);
    stopped = true;
  }

  // Wake up the scheduler thread to complete its shutdown
  scheduleChanged.notify_one();

  // Wait for the scheduler thread to exit
  schedulerThread.join();
}

class Scheduler::ScheduledTask : public network::IOCompletionHandler {
  public:
    ScheduledTask(std::unique_ptr<Task> task, std::chrono::high_resolution_clock::time_point timePoint) : task(std::move(task)), timePoint(timePoint) {}


    virtual void HandleIOCompletion(DWORD bytesTransferred, OVERLAPPED* overlapped) override {
      task->Run();
      // We must manually delete this object after it completes running
      delete this;
    }

    void Cancel() {
      // Only called after stopping the scheduler thread during cleanup
      task->Cancelled();
      delete this;
    }

    const std::chrono::high_resolution_clock::time_point timePoint; // the point in time when this task is scheduled to run
  private:
    std::unique_ptr<Task> task;
};



void Scheduler::RunAt(std::chrono::high_resolution_clock::time_point timePoint, std::unique_ptr<Task> task) {
  // We only need to notify the scheduler thread about the change if the new task is the next one to be run, so keep track of this.
  bool wasInsertedAtFirstPos;
  
  {
    std::scoped_lock lock(schedulerMutex);
    // Perform sorted insertion by the timepoint of scheduling the task
    auto insertPos = std::upper_bound(scheduledTasks.begin(), scheduledTasks.end(), timePoint, [](auto& t1, const ScheduledTask* t2) { return t1 < t2->timePoint; });
    wasInsertedAtFirstPos = (insertPos == scheduledTasks.begin()); // This will also work for an empty list, because there begin() == end()
    scheduledTasks.insert(insertPos, new ScheduledTask(std::move(task), timePoint));
  }

  // Notify the scheduler thread if necessary
  if (wasInsertedAtFirstPos) {
    scheduleChanged.notify_one();
  }
}


void Scheduler::RunIn(std::chrono::milliseconds ms, std::unique_ptr<Task> task) {
  RunAt(std::chrono::high_resolution_clock::now() + ms, std::move(task));
}


void Scheduler::SchedulerThread() {
  SetThreadDescription(GetCurrentThread(), L"blobs.db server task scheduler thread");

  std::unique_lock lock(schedulerMutex);
  while (!stopped) {
    std::optional<std::chrono::milliseconds> nextTaskDeadline;

    // First run all tasks that are due in the server thread by posting them to the IOCompletionPort
    for (nextTaskDeadline = NextTaskDeadline(); nextTaskDeadline && nextTaskDeadline->count() <= 0; nextTaskDeadline = NextTaskDeadline()) {
      ioCompletionPort.PostIOCompletionPacket(scheduledTasks.front());
      scheduledTasks.pop_front();
    }

    if (nextTaskDeadline) {
      // We have a next task to wait for
      scheduleChanged.wait_for(lock, *nextTaskDeadline);
    } else {
      // We have no tasks scheduled, wait for either a task to be scheduled or the scheduler to be stopped
      scheduleChanged.wait(lock);
    }
  }

  // Scheduler thread has been stopped -> run the cancel handlers of all scheduled tasks (in this thread)
  for (auto& task : scheduledTasks) {
    // Calling Cancel() will also delete the task object itself
    task->Cancel();
  }
}



std::optional<std::chrono::milliseconds> Scheduler::NextTaskDeadline() const {
  if (scheduledTasks.empty()) {
    return std::nullopt;
  } else {
    return std::chrono::duration_cast<std::chrono::milliseconds>(scheduledTasks.front()->timePoint - std::chrono::high_resolution_clock::now());
  }
}


}