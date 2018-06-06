#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <vector>
#include <thread>
#include <sstream>
#include <type_traits>
#include <boost/crc.hpp>

// the most used ones
using std::cout;
using std::endl;
using std::string;
using std::lock_guard;
using std::mutex;
using std::atomic;
using std::vector;
using std::thread;

/* 
TO DO:
- split this file into header and source files & add Makefile
- exception handling
- some refactoring (e.g. changing types or/and names of some variables)
- better structuring and encapsulation
- get rid of inconsistency and strange design decisions (mostly made during the early development stages)
- add input via command-line arguments
- add output to a file 
- finish crc32 calculating consumer functionality
*/

namespace utils {
class SpinMutex
{
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
public:
    SpinMutex() : flag_(ATOMIC_FLAG_INIT) {}
    inline void lock() { while (flag_.test_and_set(std::memory_order_acquire)); }
    inline void unlock() { flag_.clear(std::memory_order_release); }
};

struct SafeCout : std::stringstream 
{
    mutex mtx;
    ~SafeCout()
    {
        lock_guard<mutex> lck(mtx);
        cout << rdbuf();
        cout.flush();
    }
};
} // namespace utils

// TODO:
// - make this a template class to add option for choosing underlying containers
// - apply the policy-based design: e.g. choose lock types (mutex, spinlock, lock-free, etc.)
// - use futures/packaged_tasks/async and exception handling

class DataBlockProducer
{
    const size_t n_threads_;
    size_t n_blocks_;
    const size_t block_len_;
    
    std::deque<vector<string>> queue_;
    vector<thread> A_;
    mutex q_sync_mtx_;

    vector<string> blocks_bunch_;
    atomic<int> n_done_;
    utils::SpinMutex smtx_;

    mutex mtx_;
    std::condition_variable cv_;
    bool flag_ = false;

    // Get the amount of data blocks to generate for the next iteration
    // Usually this will be equal to the generator threads number
    // Except for cases when there's too few blocks to produce
    inline size_t next_job_size()
    {
        return (n_blocks_ <= n_threads_) ? n_blocks_ : n_threads_;
    }

public:
    DataBlockProducer(const size_t& thread_number, const size_t& block_number, const size_t& block_size) : 
        n_threads_(thread_number), 
        n_blocks_(block_number),
        block_len_(block_size)
    {
        auto sz = next_job_size();
        blocks_bunch_.resize(sz);
        n_done_ = sz;
    }

    void worker(size_t id)
    {
        string rand_data;
        std::random_device rdev;
        std::uniform_int_distribution<char> ch_distr(' ', 'z');
        rand_data.reserve(block_len_);
        
        // TODO: break when the needed amount of blocks is generated
        for (;;)
        {   
            // TODO: move to a separate function
            // make more generic, add random engine like mt19937
            // - use iterators and STL algorithms like std::accumulate
            for (size_t i = 0; i < block_len_; ++i)
                rand_data += ch_distr(rdev);
            
            blocks_bunch_[id] = rand_data;

            int idx = n_done_.fetch_sub(1, std::memory_order_relaxed);
            if (idx > 0)
            {
                std::unique_lock<mutex> lck(mtx_);
                cv_.wait(lck, [this] { return flag_; });
            }
            else
            {
                for (size_t z = 0; z < blocks_bunch_.size(); ++z)
                {
                    utils::SafeCout() << "\nBlock #" << (n_blocks_ - z) << "(thread #" << z << ")" << endl;
                    utils::SafeCout() << "vec[" << z << "] = " << blocks_bunch_[z] << endl;
                }

                lock_guard<mutex> lck(mtx_);
                n_blocks_ -= blocks_bunch_.size();
                // TODO: std::move the contents of blocks_bunch_ to the queue_
                cv_.notify_all();
            }
        }
    }

    void run()
    {
        // TODO: replace with futures and detach
        for (size_t i = 0; i < n_threads_; ++i) 
            A_.emplace_back(&DataBlockProducer::worker, this, i);
        
        for_each(A_.begin(), A_.end(), [](thread& t) { if (t.joinable()) t.join(); });
    }
};


// TODO: make numeric parameters templated and use type traits like is_integral, is_unsigned and so on
// Need to completely rewrite the class (except get_crc32 method and usage of std::set to detect computation error)
class CRC32Calculator
{
    const size_t n_threads_;

    vector<thread> B_;
    std::set<decltype(boost::crc_32_type().checksum())> check_set_ {};
    
public:
    CRC32Calculator(const size_t& Bthread_number) : n_threads_(Bthread_number) {}

    decltype(auto) get_crc32(const string& my_string)
    {
        boost::crc_32_type result;
        result.process_bytes(my_string.data(), my_string.length());
        return result.checksum();
    }

    /*void run_check()
    {  
        utils::SpinMutex smtx;
        mutex mtx;
        atomic<size_t> num_ready{0};
        bool ready_flag = false;
        condition_variable ready_cvar;

        vector<string> blocks = {}; // TODO: std::move from queue_
        
        for (size_t i = 0; i < n_threads_; ++i)
        {
            B_.emplace_back(thread([&, this]()
            {
                for (size_t j = 0; j < blocks.size(); ++j)
                {
                    auto ch_sum = get_crc32(blocks[j]);
                
                    mtx.lock();
                    check_set_.insert(ch_sum);
                    mtx.unlock();

                    // ...
            }));
        
        //std::for_each(B.begin(), B.end(), [](thread& t) { if (t.joinable()) t.join(); });
    }
    */

    /*
    void run()
    {
        prod_.run();
        run_check();
    }
    */
};

int main(int argc, char* argv[])
{
    DataBlockProducer prod(5, 35, 10);
    prod.run();

    return 0;
}