#include <iostream>
#include <thread>
#include <RudpSocket.hpp>
#include <PacketHelper.hpp>
#include <protocol.hpp>
#include <Plugin.hpp>
void run_server(std::atomic<bool>& running) {
    UdpServer server;
    server.register_plugin(std::make_shared<ConsoleLogPlugin>("Server"));
    if (!server.start(9000)) {
        // 保持主 loop 不退出
        std::cerr << "Failed to start server\n";
        return;
    }
    running = true;
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // 可放一些 debug/log/stats
    }

    server.stop();
}

void run_client(std::atomic<bool>& running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 等 server 起來
    auto plugin = std::make_shared<ConsoleLogPlugin>("Client");

    UdpClient client;
    client.register_plugin(plugin);
    if(!client.start("127.0.0.1", 9000)){
        std::cerr << "Failed to start client\n";
        return;
    }

    for (int i = 0; i < 10; ++i) {
        std::string msg = "Packet #" + std::to_string(i);
        packet pkt = PacketHelper::create_data_packet(i + 1, std::vector<uint8_t>(msg.begin(), msg.end()));

        client.send_packet(pkt);
        std::this_thread::sleep_for(std::chrono::milliseconds(2)); // 模擬發送間隔
    }


    client.stop();
    running = false; // 告訴 main thread 或 server thread 可以結束了

}

int main() {
    std::atomic<bool> running{true};
    std::thread serverThread([&]() { run_server(running); });
    std::thread clientThread([&]() { run_client(running); });
    
    // keep the main thread alive until server and client threads finish
    while (running) {
        std::cout << "Main thread running... " << running << " \n";
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        // 可放一些 debug/log/stats
    }

    serverThread.join();
    clientThread.join();

    return 0;
}
