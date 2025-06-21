#include <blobs/Blobs.hpp>

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

// You can add a delay to see the server's round robin lock scheduling in effect
constexpr int sleepMs = 0;

struct VectorClock {
  VectorClock(const std::vector<int>& clock, size_t index) : data(clock), index(index) {}

  const std::vector<int>& data;
  size_t index;
};


// Nicely print out the current state of the vector clock
std::ostream& operator<<(std::ostream& out, const VectorClock& clock) {

  for (size_t i = 0; i < clock.data.size(); ++i) {
    if (i != 0) {
      out << " - ";
    }

    if (i == clock.index) {
      // Mark this client's clock value
      out << '[' << std::setw(3) << clock.data[i] << ']';
    } else {
      out << ' ' << std::setw(3) << clock.data[i] << ' ';
    }
  }

  return out;
}



int main() {
  try {
    blobs::Initialize();

    std::cout << "Opening database...\n";
    auto db = blobs::Database::Open("127.0.0.1", "vector-clock.db");
    std::cout << "Database opened.\n";

    auto initialClock = db->ReadVector<int>(0, 0, 0, true);
    if (initialClock.empty()) {
      // This is the first client -> print instructions and wait for user to confirm
      std::cout
        << "== Vector clock test ==\n\n"
        << "This is the first client. Start as many other clients as you wish to run in parallel.\n"
        << "AFTER the clients have opened the database, press any key in this client to actually start the test.\n\n"
        << "The test consists of each client counting up its own counter in the same blob. "
        << "The clients should be synchronized through the database's write locks. "
        << "Only one client can read&write the blob with the vector clock at a time. "
        << "Once all clients have finished the clock should be at 100 for each client.\n\n"
      ;

      system("PAUSE");
    }


    auto index = initialClock.size();
    initialClock.push_back(0);

    std::cout << "\n\nClient's clock index is: " << index << "\n";
    std::cout << "Initial Clock: " << VectorClock(initialClock, index) << '\n';

    db->WriteVector(0, 0, 0, initialClock);
    blobs::Transaction::Commit();

    // If synchronization works correctly, then after 100 increment steps the vector clock will be at 100 for this client
    for (int i = 0; i < 100; ++i) {
      std::cout << "Waiting for write lock...\n";
      auto currentClock = db->ReadVector<int>(0, 0, 0, true);
      std::cout << "Reading Clock: " << VectorClock(currentClock, index) << '\n';
      if (sleepMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
      }
      ++currentClock[index];
      std::cout << "Writing Clock: " << VectorClock(currentClock, index) << '\n';
      db->WriteVector(0, 0, 0, currentClock);
      blobs::Transaction::Commit();
    }

    TODO("Convert this into a doctest which can automatically check correct increment of the clock across all clients if run with --test");

    std::cout << "Vector clock completed counting. Closing database...\n";
    db->Close();

    blobs::Shutdown();

    std::cout << "Waiting for input to exit\n";
    system("PAUSE");
  } catch (blobs::Exception& ex) {
    std::cerr << "[ERR] Exiting client with exception: " << ex.what() << "\n";
  }
}