#include "pch.hpp"

using namespace blobs;



TEST_CASE("Open same databsae in two sessions in one thread") {
  auto sessionA = Session::Create();
  auto sessionB = Session::Create();

  database_ptr dbA;
  REQUIRE_NOTHROW_MESSAGE(dbA.reset(Database::Open(sessionA, "localhost", "mem:openIn2Sessions.db")), "Opening the database in the first session shouldn't fail");

  database_ptr dbB;
  REQUIRE_NOTHROW_MESSAGE(dbB.reset(Database::Open(sessionB, "localhost", "mem:openIn2Sessions.db")), "Opening the same database in the second session shouldn't fail");
}