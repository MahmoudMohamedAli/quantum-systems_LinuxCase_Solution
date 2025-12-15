#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class UdpScheduler {
public:
    UdpScheduler();
    ~UdpScheduler();

    void sendNow(const std::string& ip, uint16_t port,
                 const std::vector<uint8_t>& data);

    void sendAfter(uint8_t delaySeconds,
                   const std::string& ip, uint16_t port,
                   const std::vector<uint8_t>& data);

    int sendPeriodic(uint8_t intervalSeconds,
                     const std::string& ip, uint16_t port,
                     const std::vector<uint8_t>& data);

    void cancelPeriodic(int taskId);

private:
    struct Task {
        std::chrono::steady_clock::time_point nextTime;
        uint8_t interval;          // 0 = one-shot
        int id;
        sockaddr_in addr;
        std::vector<uint8_t> payload;
        bool operator>(const Task& other) const {
            return nextTime > other.nextTime;
        }
    };

    void workerLoop();
    void sendPacket(const Task& task);

    int sockfd;
    std::atomic<bool> running{true};
    std::thread worker;

    std::priority_queue<Task, std::vector<Task>, std::greater<>> queue;
    std::mutex mtx;
    std::condition_variable cv;

    std::atomic<int> idCounter{1};
};

/* ================= IMPLEMENTATION ================= */

UdpScheduler::UdpScheduler() {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    worker = std::thread(&UdpScheduler::workerLoop, this);
}

UdpScheduler::~UdpScheduler() {
    running = false;
    cv.notify_all();
    worker.join();
    close(sockfd);
}

void UdpScheduler::sendNow(const std::string& ip, uint16_t port,
                           const std::vector<uint8_t>& data) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    sendto(sockfd, data.data(), data.size(), 0,
           reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

void UdpScheduler::sendAfter(uint8_t delaySeconds,
                             const std::string& ip, uint16_t port,
                             const std::vector<uint8_t>& data) {
    Task task{};
    task.interval = 0;
    task.id = 0;
    task.nextTime = std::chrono::steady_clock::now()
                    + std::chrono::seconds(delaySeconds);
    task.payload = data;

    task.addr.sin_family = AF_INET;
    task.addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &task.addr.sin_addr);

    {
        std::lock_guard<std::mutex> lock(mtx);
        queue.push(task);
    }
    cv.notify_one();
}

int UdpScheduler::sendPeriodic(uint8_t intervalSeconds,
                               const std::string& ip, uint16_t port,
                               const std::vector<uint8_t>& data) {
    Task task{};
    task.interval = intervalSeconds;
    task.id = idCounter++;
    task.nextTime = std::chrono::steady_clock::now()
                    + std::chrono::seconds(intervalSeconds);
    task.payload = data;

    task.addr.sin_family = AF_INET;
    task.addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &task.addr.sin_addr);

    {
        std::lock_guard<std::mutex> lock(mtx);
        queue.push(task);
    }
    cv.notify_one();
    return task.id;
}

void UdpScheduler::cancelPeriodic(int taskId) {
    std::lock_guard<std::mutex> lock(mtx);
    std::priority_queue<Task, std::vector<Task>, std::greater<>> newQueue;

    while (!queue.empty()) {
        auto t = queue.top();
        queue.pop();
        if (t.id != taskId) {
            newQueue.push(t);
        }
    }
    queue.swap(newQueue);
}

void UdpScheduler::workerLoop() {
    while (running) {
        std::unique_lock<std::mutex> lock(mtx);

        if (queue.empty()) {
            cv.wait(lock);
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        auto task = queue.top();

        if (task.nextTime > now) {
            cv.wait_until(lock, task.nextTime);
            continue;
        }

        queue.pop();
        lock.unlock();

        sendPacket(task);

        if (task.interval > 0 && running) {
            task.nextTime = now + std::chrono::seconds(task.interval);
            std::lock_guard<std::mutex> relock(mtx);
            queue.push(task);
        }
    }
}

void UdpScheduler::sendPacket(const Task& task) {
    sendto(sockfd, task.payload.data(), task.payload.size(), 0,
           reinterpret_cast<const sockaddr*>(&task.addr),
           sizeof(task.addr));
}

int main() {
    UdpScheduler udp;

    udp.sendNow("127.0.0.1", 5000, {'H','i'});

    udp.sendAfter(5, "127.0.0.1", 5001, {'D','e','l','a','y'});

    int id = udp.sendPeriodic(2, "127.0.0.1", 5002, {'P','i','n','g'});

    sleep(10);
    udp.cancelPeriodic(id);
}
