#include <blobs/Blobs.hpp>

#include <iostream>
#include <iomanip>


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
      out << '>' << std::setw(3) << clock.data[i] << '<';
    } else {
      out << ' ' << std::setw(3) << clock.data[i] << ' ';
    }
  }

  return out;
}



int main() {
  try {
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
    std::cout << "Initial Clock: " << VectorClock(initialClock, index);


    TODO("Commit this blob and update the vector blob in each transaction... maybe with a small time delay?");



    db->Close();
    std::cout << "Exiting client\n";
  } catch (blobs::Exception& ex) {
    std::cerr << "[ERR] Exiting client with exception: " << ex.what() << "\n";
  }
}