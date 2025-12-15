#include <chrono>
#include <atomic>
#include <memory>
#include <thread>
#include <functional>
#include <iostream>

using namespace std::chrono_literals;

void StartThread(
    std::thread& thread,
    std::atomic<bool>& running,
    const std::function<bool(void)> Process,
    const std::chrono::seconds timeout)
{

    thread = std::thread(
        [&running, Process = std::move(Process), timeout] ()
        {
             auto start = std::chrono::high_resolution_clock::now();
            
            while(running)
            {
               
                bool aborted = Process();

                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                auto timeout_s = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
                if (aborted || duration > timeout_s)
                {
                    running = false;
                    break;
                }
            }
        });
}

int main(int argc, char **argv)
{
    std::atomic<bool> my_running = true;
    std::atomic<bool> my_running_1 = true;
    std::atomic<bool> my_running_2 = true;
    std::thread my_thread1, my_thread2;
    int loop_counter1 = 0, loop_counter2 = 0;

    // start actions in seprate threads and wait of them

    StartThread(
        my_thread1,
        my_running_1, 
        [&]()
        {
            // "some actions" simulated with waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            loop_counter1++;
            std::cout<<"Loop1 count: "<<loop_counter1<<std::endl;
            return false;
        },
        10s); // loop timeout

    StartThread(
        my_thread2,
        my_running_2, 
        [&]()
        {
            // "some actions" simulated with waiting 
            if (loop_counter2 < 5)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
              
                loop_counter2++;
                  std::cout<<"Loop2 count: "<<loop_counter2<<std::endl;
                return false;
            }
//std::cout<<"Loop2 count: "<<loop_counter2<<std::endl;
            return true;
        },
        10s); // loop timeout


    my_thread1.join();
    my_thread2.join();

    // print execlution loop counters
    std::cout << "C1: " << loop_counter1 << " C2: " << loop_counter2 << std::endl;
}