#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
//#include <functional>
#include <iostream>
#include <memory>
#include <utility>
#include <mutex>
//#include <fstream>
#include <random>
#include <set>
#include <future>
#include <string>
#include <vector>
#include <thread>
#include <sstream>
//#include <type_traits>
#include <boost/crc.hpp>

using namespace std::chrono_literals;

using std::cout;
using std::endl;
using std::string;
using std::lock_guard;
using std::mutex;
using std::atomic;
using std::vector;
using std::thread;
using std::unique_ptr;
using std::promise;
using std::future;
using std::make_unique;
using std::unique_lock;
using std::uniform_int_distribution;
using std::random_device;
using std::deque;
using std::condition_variable;
using std::once_flag;
using std::call_once;
using std::move;

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

template <typename EngineT = std::default_random_engine>
void fill_with_random_values(string& str, std::pair<char, char> range)
{
    static random_device rdev;
    static EngineT engine {};
    engine.seed(rdev());
    static uniform_int_distribution<char> uniform_distr(range.first, range.second);

    for (auto& element : str)
        element = uniform_distr(engine);
}

class DataBlockProducer
{
    using str_vector_t = vector<string>;

    const size_t n_threads_;
    size_t n_blocks_;
    const size_t block_len_;
    
    vector<vector<string>> queue_;
    vector<thread> A_;
    mutex q_sync_mtx_;

    unique_ptr<str_vector_t> blocks_bunch_;
    atomic<int> count_;

    once_flag once_f_;
    mutex mtx_, mtx2_;
    atomic<bool> flag_{false};
    atomic<bool> done_{false};

    unique_ptr<promise<void>> prom_ptr_;
    future<void> fut_;

    string dummy_str_;

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
        block_len_(block_size),
        prom_ptr_(make_unique<promise<void>>()),
        fut_(prom_ptr_->get_future())
    {
        count_ = next_job_size();
        dummy_str_.assign(block_len_, ' ');
        blocks_bunch_ = make_unique<str_vector_t>(count_, dummy_str_);
    }

    void worker(size_t id)
    {
        // TODO: break when the needed amount of blocks is generated
        for (;!done_.load();flag_.store(false))
        {   
            // TODO: move to a separate function
            // make more generic, add random engine like mt19937
            auto& str = (*blocks_bunch_)[id];

            fill_with_random_values(str, std::make_pair<char, char>(' ' + 1, 'z'));

            if (--count_ > 0)
            {
                while (!flag_.load())
                    std::this_thread::sleep_for(5ms);
            }
            else
            {
                for (size_t z = 0; z < blocks_bunch_->size(); ++z)
                {
                    cout << "\nBlock #" << (n_blocks_ - z) << "(thread #" << z << ")" << endl;
                    cout << "vec[" << z << "] = " << (*blocks_bunch_)[z] << endl;
                }
                    
                auto sz = (*blocks_bunch_).size();
                if (sz >= n_blocks_)
                {
                    done_.store(true);
                    break;
                }
                n_blocks_ -= (*blocks_bunch_).size();
                queue_.emplace_back(move(*blocks_bunch_));

                count_.store(next_job_size(), std::memory_order::memory_order_release);
                blocks_bunch_ = make_unique<str_vector_t>();
                blocks_bunch_->assign(count_, dummy_str_);
            }
            flag_.store(true);
        }

        call_once(once_f_, [this] { prom_ptr_->set_value(); });
    }

    void run()
    {
        // TODO: replace with futures and detach
        for (size_t i = 0; i < n_threads_; ++i) 
            A_.emplace_back(&DataBlockProducer::worker, this, i);
        
        fut_.get();
        if (!fut_.valid())
            cout << "\nLOOOL\n";
        
        for_each(A_.begin(), A_.end(), [](thread& t) { if (t.joinable()) t.join(); });
    }
};


// TODO: make numeric parameters templated and use type traits like is_integral, is_unsigned and so on
// Need to completely rewrite the class (except get_crc32 method and usage of std::set to detect computation error)
class CRC32Calculator
{
    //const size_t n_threads_;

    vector<thread> B_;
    std::set<decltype(boost::crc_32_type().checksum())> check_set_ {};
    
public:
    CRC32Calculator(const size_t& Bthread_number) {} //: n_threads_(Bthread_number) {}

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
    DataBlockProducer prod(3, 9, 10);
    prod.run();

    return 0;
}