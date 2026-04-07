#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "Rudp/Config.hpp"

namespace {

TEST(ConfigYamlTest, LoadsClientProfileWithChannels) {
  const auto path =
      std::filesystem::temp_directory_path() / "rudp_config_client_test.yaml";
  std::ofstream output(path);
  output << "mode: client\n"
            "runtime:\n"
            "  log_path: logs/test_client.log\n"
            "connection:\n"
            "  bind_address: 0.0.0.0\n"
            "  bind_port: 0\n"
            "  remote_address: 127.0.0.1\n"
            "  remote_port: 9010\n"
            "channels:\n"
            "  - id: 7\n"
            "    name: chat\n"
            "    type: reliable_ordered\n"
            "    default: true\n"
            "  - id: 8\n"
            "    name: state\n"
            "    type: unreliable\n";
  output.close();

  Rudp::Config::RuntimeProfile profile;
  std::string error;
  ASSERT_TRUE(
      Rudp::Config::load_runtime_profile_from_yaml(path, profile, &error))
      << error;

  EXPECT_EQ(profile.mode, Rudp::Config::RuntimeMode::Client);
  EXPECT_EQ(profile.remote_address, "127.0.0.1");
  EXPECT_EQ(profile.remote_port, 9010);
  ASSERT_EQ(profile.channels.size(), 2U);
  EXPECT_EQ(profile.channels[0].id, 7U);
  EXPECT_EQ(profile.channels[0].name, "chat");
  EXPECT_EQ(profile.channels[0].type, Rudp::ChannelType::ReliableOrdered);
  EXPECT_TRUE(profile.channels[0].is_default);
}

TEST(ConfigYamlTest, DefaultsChannelWhenProfileOmitsChannels) {
  const auto path =
      std::filesystem::temp_directory_path() / "rudp_config_server_test.yaml";
  std::ofstream output(path);
  output << "mode: server\n"
            "connection:\n"
            "  bind_address: 127.0.0.1\n"
            "  bind_port: 9020\n";
  output.close();

  Rudp::Config::RuntimeProfile profile;
  std::string error;
  ASSERT_TRUE(
      Rudp::Config::load_runtime_profile_from_yaml(path, profile, &error))
      << error;

  EXPECT_EQ(profile.mode, Rudp::Config::RuntimeMode::Server);
  ASSERT_EQ(profile.channels.size(), 1U);
  EXPECT_EQ(profile.channels[0].id, 1U);
  EXPECT_EQ(profile.channels[0].name, "default");
  EXPECT_EQ(profile.channels[0].type, Rudp::ChannelType::Unreliable);
  EXPECT_TRUE(profile.channels[0].is_default);
}

}  // namespace
