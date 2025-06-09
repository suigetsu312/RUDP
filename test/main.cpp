#include <iostream>
#include <thread>
#include <RudpSocket.hpp>
#include <PacketHelper.hpp>
#include <protocol.hpp>
#include "ConsoleLogPlugin.hpp"

void run_server() {
    UdpServer server;
    server.register_plugin(std::make_shared<ConsoleLogPlugin>("Server"));
    if (server.start(9000)) {
        // 保持主 loop 不退出
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void run_client() {
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 等 server 起來
    auto plugin = std::make_shared<ConsoleLogPlugin>("Client");

    UdpClient client;
    client.register_plugin(plugin);
    client.connect_to("127.0.0.1", 9000);
    for (int i = 0; i < 10; ++i) {
        std::string msg = "Packet #" + std::to_string(i);
        packet pkt = PacketHelper::create_data_packet(i + 1, std::vector<uint8_t>(msg.begin(), msg.end()));

        client.send_packet(pkt);
        std::this_thread::sleep_for(std::chrono::milliseconds(2)); // 模擬發送間隔
    }
}

int main() {
    std::thread serverThread(run_server);
    std::thread clientThread(run_client);

    serverThread.detach();
    clientThread.join();

    return 0;
}
