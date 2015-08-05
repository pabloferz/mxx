#include <mpi.h>
#include <gtest/gtest.h>
#include <iostream>

namespace mpi_gtest
{
using namespace ::testing;

// wrap around another event listener and forward only on processor with rank 0
class MpiTestEventListener : public TestEventListener {
  private:
    int rank;
    TestEventListener* other;
  public:
    MpiTestEventListener(int rank, TestEventListener* other)
      : rank(rank), other(other) {}
    virtual ~MpiTestEventListener() {}

    // Fired before any test activity starts.
    virtual void OnTestProgramStart(const UnitTest& unit_test)
    {
      if (rank == 0)
        other->OnTestProgramStart(unit_test);
    }

    // Fired before each iteration of tests starts.  There may be more than
    // one iteration if GTEST_FLAG(repeat) is set. iteration is the iteration
    // index, starting from 0.
    virtual void OnTestIterationStart(const UnitTest& unit_test,
        int iteration)
    {
      if (rank == 0)
        other->OnTestIterationStart(unit_test, iteration);
    }

    // Fired before environment set-up for each iteration of tests starts.
    virtual void OnEnvironmentsSetUpStart(const UnitTest& unit_test)
    {
      if(rank == 0)
        other->OnEnvironmentsSetUpStart(unit_test);
    }

    // Fired after environment set-up for each iteration of tests ends.
    virtual void OnEnvironmentsSetUpEnd(const UnitTest& unit_test)
    {
      if(rank == 0)
        other->OnEnvironmentsSetUpEnd(unit_test);
    }

    // Fired before the test case starts.
    virtual void OnTestCaseStart(const TestCase& test_case)
    {
      if(rank == 0)
        other->OnTestCaseStart(test_case);
    }

    // Fired before the test starts.
    virtual void OnTestStart(const TestInfo& test_info)
    {
      if (rank == 0)
        other->OnTestStart(test_info);
    }

    // Fired after a failed assertion or a SUCCEED() invocation.
    virtual void OnTestPartResult(const TestPartResult& test_part_result)
    {
      if (rank == 0)
        other->OnTestPartResult(test_part_result);
    }

    // Fired after the test ends.
    virtual void OnTestEnd(const TestInfo& test_info)
    {
        // serialze all failed TestPartResults and send to rank 0
        // there, receive and ADD_FAILURE_AT() for the remote error

        // first send lineno, since -1 is used by google for unknown line numbers
        // we use -13 for "no more fails"
        if (rank != 0) {
            if (!test_info.result()->Passed()) {
                int count = test_info.result()->total_part_count();
                for (int i = 0; i < count; ++i) {
                    if (!test_info.result()->GetTestPartResult(i).passed()) {
                        // get result information for failed tests
                        int line = test_info.result()->GetTestPartResult(i).line_number();
                        std::string filename(test_info.result()->GetTestPartResult(i).file_name());
                        std::string msg(test_info.result()->GetTestPartResult(i).message());
                        // send info to rank 0
                        // TODO: replace with mxx calls?
                        // TODO: custom communicator?
                        // TODO: use custom `tag`? currently all are tag `0`
                        MPI_Send(&line, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
                        MPI_Send(&filename[0], filename.size(), MPI_CHAR, 0, 0, MPI_COMM_WORLD);
                        MPI_Send(&msg[0], msg.size(), MPI_CHAR, 0, 0, MPI_COMM_WORLD);
                    }
                }
            }
            // send the END message (line = -13)
            int end = -13;
            MPI_Send(&end, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
        } else {
            // rank 0: in a for loop over all ranks, receive the failing messages
            int p;
            MPI_Comm_size(MPI_COMM_WORLD, &p);
            for (int i = 1; i < p; ++i)
            {
                int line;
                std::string filename;
                std::string message;
                MPI_Recv(&line, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                while (line != -13) {
                    // receive filename and message
                    // TODO: use mxx std::string overloads for recv
                    MPI_Status stat;
                    int len;
                    // filename
                    MPI_Probe(i, 0, MPI_COMM_WORLD, &stat);
                    MPI_Get_count(&stat, MPI_CHAR, &len);
                    filename.resize(len);
                    MPI_Recv(&filename[0], len, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    // message
                    MPI_Probe(i, 0, MPI_COMM_WORLD, &stat);
                    MPI_Get_count(&stat, MPI_CHAR, &len);
                    message.resize(len);
                    MPI_Recv(&message[0], len, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                    ADD_FAILURE_AT(filename.c_str(), line) << "[On Rank " << i << "] " << message;

                    // receive next line or end message
                    MPI_Recv(&line, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
            }
        }
        if (rank == 0)
          other->OnTestEnd(test_info);
    }

    // Fired after the test case ends.
    virtual void OnTestCaseEnd(const TestCase& test_case)
    {
      if (rank == 0)
        other->OnTestCaseEnd(test_case);
    }

    // Fired before environment tear-down for each iteration of tests starts.
    virtual void OnEnvironmentsTearDownStart(const UnitTest& unit_test)
    {
      if (rank == 0)
        other->OnEnvironmentsTearDownStart(unit_test);
    }

    // Fired after environment tear-down for each iteration of tests ends.
    virtual void OnEnvironmentsTearDownEnd(const UnitTest& unit_test)
    {
      if (rank == 0)
        other->OnEnvironmentsTearDownEnd(unit_test);
    }

    // Fired after each iteration of tests finishes.
    virtual void OnTestIterationEnd(const UnitTest& unit_test,
        int iteration)
    {
      if (rank == 0)
        other->OnTestIterationEnd(unit_test, iteration);
    }

    // Fired after all test activities have ended.
    virtual void OnTestProgramEnd(const UnitTest& unit_test)
    {
      if (rank == 0)
        other->OnTestProgramEnd(unit_test);
    }
};

}

int main(int argc, char* argv[]) {
    int result = 0;

    ::testing::InitGoogleTest(&argc, argv);
    MPI_Init(&argc, &argv);

    // get processor rank
    int rank, p;
    MPI_Comm_size(MPI_COMM_WORLD, &p);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == 0)
      std::cout << "Running GTEST with MPI using " << p << " processes." << std::endl;

    // set up wrapped test listener
    ::testing::TestEventListeners& listeners =
      ::testing::UnitTest::GetInstance()->listeners();
    ::testing::TestEventListener* default_listener =  listeners.Release(listeners.default_result_printer());
    listeners.Append(new mpi_gtest::MpiTestEventListener(rank, default_listener));

    // running tests
    result = RUN_ALL_TESTS();
    if (rank != 0)
      result = 0;

    // clean up MPI
    MPI_Finalize();

    // return good status no matter what
    return result;
}