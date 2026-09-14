#include "MetricsServer.hpp"
#include <chrono>
#include <stdexcept>

struct MetricsServerTest {
    static void start(MetricsServer& server) {
        server.started_ = true;
        server.stopping_ = false;
        server.snapLine_ = "seq=1";
        server.snapSeq_ = 1;
        // Exercise the real socket lifecycle without sampling host hardware.
        server.acceptThread_ = std::thread([&] { server.acceptLoop(); });
    }
    static size_t count(MetricsServer& server) {
        {
            std::lock_guard<std::mutex> lock(server.snapMx_);
            ++server.snapSeq_;
        }
        server.reapClients();
        std::lock_guard<std::mutex> lock(server.clientMx_);
        return server.clients_.size();
    }
    static int connectClient() {
        for (int i = 0; i < 200; ++i) {
            int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            sockaddr_un addr{};
            auto len = MetricsServer::fillAddr(addr);
            if (connect(fd, reinterpret_cast<sockaddr*>(&addr), len) == 0) return fd;
            close(fd);
            usleep(10000);
        }
        throw std::runtime_error("connect timed out");
    }
};

int main() {
    std::atomic<bool> run{true};
    MetricsServer server(run);
    MetricsServerTest::start(server);
    for (int round = 0; round < 12; ++round) {
        for (int i = 0; i < 8; ++i) {
            int fd = MetricsServerTest::connectClient();
            send(fd, "K1\n", 3, MSG_NOSIGNAL);
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
        // Allow accept() to register queued connections before checking reclamation.
        usleep(100000);
        for (int i = 0; i < 200 && MetricsServerTest::count(server); ++i) usleep(10000);
        if (MetricsServerTest::count(server) != 0) return 1;
    }
    int live = MetricsServerTest::connectClient();
    auto begin = std::chrono::steady_clock::now();
    server.stop();
    close(live);
    if (MetricsServerTest::count(server) != 0) return 2;
    if (std::chrono::steady_clock::now() - begin > std::chrono::seconds(3)) return 3;
    // A new connection racing with stop must also be reclaimed.
    for (int i = 0; i < 10; ++i) {
        MetricsServerTest::start(server);
        live = MetricsServerTest::connectClient();
        server.stop();
        close(live);
        if (MetricsServerTest::count(server) != 0) return 4;
    }
    std::puts("PASS: repeated reconnects, active-client stop, accept/stop race");
}
