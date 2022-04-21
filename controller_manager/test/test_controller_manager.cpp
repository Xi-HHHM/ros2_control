// Copyright 2020 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "controller_interface/controller_interface.hpp"
#include "controller_manager/controller_manager.hpp"
#include "controller_manager_msgs/srv/list_controllers.hpp"
#include "controller_manager_test_common.hpp"
#include "lifecycle_msgs/msg/state.hpp"
#include "test_controller/test_controller.hpp"

using ::testing::_;
using ::testing::Return;

class TestControllerManager
: public ControllerManagerFixture<controller_manager::ControllerManager>,
  public testing::WithParamInterface<Strictness>
{
};

TEST_P(TestControllerManager, controller_lifecycle)
{
  const auto test_param = GetParam();
  auto test_controller = std::make_shared<test_controller::TestController>();
  cm_->add_controller(
    test_controller, test_controller::TEST_CONTROLLER_NAME,
    test_controller::TEST_CONTROLLER_CLASS_NAME);
  EXPECT_EQ(1u, cm_->get_loaded_controllers().size());
  EXPECT_EQ(2, test_controller.use_count());

  // Question: should we put the namespace checking in a new test?
  // setup interface to claim from controllers
  controller_interface::InterfaceConfiguration cmd_itfs_cfg;
  cmd_itfs_cfg.type = controller_interface::interface_configuration_type::INDIVIDUAL;
  for (const auto & interface : ros2_control_test_assets::TEST_ACTUATOR_HARDWARE_COMMAND_INTERFACES)
  {
    cmd_itfs_cfg.names.push_back(interface);
  }
  test_controller->set_command_interface_configuration(cmd_itfs_cfg);

  controller_interface::InterfaceConfiguration state_itfs_cfg;
  state_itfs_cfg.type = controller_interface::interface_configuration_type::INDIVIDUAL;
  for (const auto & interface : ros2_control_test_assets::TEST_ACTUATOR_HARDWARE_STATE_INTERFACES)
  {
    state_itfs_cfg.names.push_back(interface);
  }
  for (const auto & interface : ros2_control_test_assets::TEST_SENSOR_HARDWARE_STATE_INTERFACES)
  {
    state_itfs_cfg.names.push_back(interface);
  }
  test_controller->set_state_interface_configuration(state_itfs_cfg);

  /*
  controller_interface::InterfaceConfiguration cmd_itfs_cfg2;
  cmd_itfs_cfg2.type = controller_interface::interface_configuration_type::INDIVIDUAL;
  for (const auto & interface : ros2_control_test_assets::TEST_SYSTEM_HARDWARE_COMMAND_INTERFACES)
  {
    cmd_itfs_cfg2.names.push_back(interface);
  }
  test_controller2->set_command_interface_configuration(cmd_itfs_cfg2);

  controller_interface::InterfaceConfiguration state_itfs_cfg2;
  state_itfs_cfg2.type = controller_interface::interface_configuration_type::ALL;
  test_controller2->set_state_interface_configuration(state_itfs_cfg2);
  */

  // Check if namespace is set correctly
  RCLCPP_INFO(
    rclcpp::get_logger("test_controller_manager"), "Controller Manager namespace is '%s'",
    cm_->get_namespace());
  EXPECT_STREQ(cm_->get_namespace(), "/");
  RCLCPP_INFO(
    rclcpp::get_logger("test_controller_manager"), "Controller 1 namespace is '%s'",
    test_controller->get_node()->get_namespace());
  EXPECT_STREQ(test_controller->get_node()->get_namespace(), "/");
  /*  RCLCPP_INFO(
    rclcpp::get_logger("test_controller_manager"), "Controller 2 namespace is '%s'",
    test_controller2->get_node()->get_namespace());
  EXPECT_STREQ(test_controller2->get_node()->get_namespace(), "/"); */

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_EQ(0u, test_controller->internal_counter)
    << "Update should not reach an unconfigured controller";

  EXPECT_EQ(
    lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, test_controller->get_state().id());

  // configure controller
  cm_->configure_controller(test_controller::TEST_CONTROLLER_NAME);
  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_EQ(0u, test_controller->internal_counter) << "Controller is not started";

  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, test_controller->get_state().id());

  // Start the real test controller, will take effect at the end of the update function
  std::vector<std::string> start_controllers = {test_controller::TEST_CONTROLLER_NAME};
  std::vector<std::string> stop_controllers = {};
  auto switch_future = std::async(
    std::launch::async, &controller_manager::ControllerManager::switch_controller, cm_,
    start_controllers, stop_controllers, test_param.strictness, true, rclcpp::Duration(0, 0));

  ASSERT_EQ(std::future_status::timeout, switch_future.wait_for(std::chrono::milliseconds(100)))
    << "switch_controller should be blocking until next update cycle";

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_EQ(0u, test_controller->internal_counter) << "Controller is started at the end of update";
  {
    ControllerManagerRunner cm_runner(this);
    EXPECT_EQ(controller_interface::return_type::OK, switch_future.get());
  }
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, test_controller->get_state().id());

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_GE(test_controller->internal_counter, 1u);
  auto last_internal_counter = test_controller->internal_counter;

  // Stop controller, will take effect at the end of the update function
  start_controllers = {};
  stop_controllers = {test_controller::TEST_CONTROLLER_NAME};
  switch_future = std::async(
    std::launch::async, &controller_manager::ControllerManager::switch_controller, cm_,
    start_controllers, stop_controllers, test_param.strictness, true, rclcpp::Duration(0, 0));

  ASSERT_EQ(std::future_status::timeout, switch_future.wait_for(std::chrono::milliseconds(100)))
    << "switch_controller should be blocking until next update cycle";

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_EQ(last_internal_counter + 1u, test_controller->internal_counter)
    << "Controller is stopped at the end of update, so it should have done one more update";
  {
    ControllerManagerRunner cm_runner(this);
    EXPECT_EQ(controller_interface::return_type::OK, switch_future.get());
  }

  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, test_controller->get_state().id());
  auto unload_future = std::async(
    std::launch::async, &controller_manager::ControllerManager::unload_controller, cm_,
    test_controller::TEST_CONTROLLER_NAME);

  ASSERT_EQ(std::future_status::timeout, unload_future.wait_for(std::chrono::milliseconds(100)))
    << "unload_controller should be blocking until next update cycle";
  ControllerManagerRunner cm_runner(this);
  EXPECT_EQ(controller_interface::return_type::OK, unload_future.get());

  EXPECT_EQ(
    lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, test_controller->get_state().id());
  EXPECT_EQ(1, test_controller.use_count());
}

TEST_P(TestControllerManager, unknown_controllers)
{
  const auto test_param = GetParam();
  auto test_controller = std::make_shared<test_controller::TestController>();
  auto test_controller_2 = std::make_shared<test_controller::TestController>();
  constexpr char TEST_CONTROLLER_2_NAME[] = "test_controller_2_name";

  cm_->add_controller(
    test_controller, test_controller::TEST_CONTROLLER_NAME,
    test_controller::TEST_CONTROLLER_CLASS_NAME);
  cm_->add_controller(
    test_controller_2, TEST_CONTROLLER_2_NAME, test_controller::TEST_CONTROLLER_CLASS_NAME);

  EXPECT_EQ(2u, cm_->get_loaded_controllers().size());

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));

  // configure controller
  cm_->configure_controller(test_controller::TEST_CONTROLLER_NAME);
  cm_->configure_controller(TEST_CONTROLLER_2_NAME);

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_EQ(0u, test_controller->internal_counter) << "Controller is not started";
  EXPECT_EQ(0u, test_controller_2->internal_counter) << "Controller is not started";

  // Start controller, will take effect at the end of the update function
  std::vector<std::string> start_controllers = {"fake_controller", TEST_CONTROLLER_2_NAME};
  std::vector<std::string> stop_controllers = {};
  auto switch_future = std::async(
    std::launch::async, &controller_manager::ControllerManager::switch_controller, cm_,
    start_controllers, stop_controllers, test_param.strictness, true, rclcpp::Duration(0, 0));

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_EQ(0u, test_controller_2->internal_counter)
    << "Controller is started at the end of update";
  {
    ControllerManagerRunner cm_runner(this);
    EXPECT_EQ(test_param.expected_return, switch_future.get());
  }

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_GE(test_controller_2->internal_counter, test_param.expected_counter);

  // Start the real test controller, will take effect at the end of the update function
  // Should test_controller be started if test_controller_2 is already active?
  start_controllers = {test_controller::TEST_CONTROLLER_NAME};
  stop_controllers = {};
  switch_future = std::async(
    std::launch::async, &controller_manager::ControllerManager::switch_controller, cm_,
    start_controllers, stop_controllers, test_param.strictness, true, rclcpp::Duration(0, 0));

  ASSERT_EQ(std::future_status::timeout, switch_future.wait_for(std::chrono::milliseconds(100)))
    << "switch_controller should be blocking until next update cycle";

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_EQ(0u, test_controller->internal_counter) << "Controller is started at the end of update";
  {
    ControllerManagerRunner cm_runner(this);
    EXPECT_EQ(controller_interface::return_type::OK, switch_future.get());
  }
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, test_controller->get_state().id());

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_GE(test_controller->internal_counter, 1u);

  // Should we expect two active controllers in best_effort?
  unsigned int active_controllers_count = 0;
  if (test_controller->get_state().id() == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE)
    ++active_controllers_count;

  if (test_controller_2->get_state().id() == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE)
    ++active_controllers_count;

  EXPECT_EQ(active_controllers_count, test_param.expected_active_controllers)
    << "The number of active controllers should be: " << test_param.expected_active_controllers;
  auto last_internal_counter = test_controller->internal_counter;
}

TEST_P(TestControllerManager, resource_conflict)
{
  const auto test_param = GetParam();
  auto test_controller = std::make_shared<test_controller::TestController>();
  auto test_controller_2 = std::make_shared<test_controller::TestController>();
  auto test_controller_3 = std::make_shared<test_controller::TestController>();

  constexpr char TEST_CONTROLLER_2_NAME[] = "test_controller_2_name";
  constexpr char TEST_CONTROLLER_3_NAME[] = "test_controller_3_name";

  // Specific the command interface configuration
  controller_interface::InterfaceConfiguration cmd_cfg = {
    controller_interface::interface_configuration_type::INDIVIDUAL,
    {"joint1/position", "joint2/velocity"}};
  controller_interface::InterfaceConfiguration state_cfg = {
    controller_interface::interface_configuration_type::INDIVIDUAL,
    {"joint1/position", "joint1/velocity", "joint2/position"}};

  // Produce resource conflict for test_controller & test_controller_3
  test_controller->set_command_interface_configuration(cmd_cfg);
  test_controller->set_state_interface_configuration(state_cfg);

  test_controller_3->set_command_interface_configuration(cmd_cfg);
  test_controller_3->set_state_interface_configuration(state_cfg);

  // Add controlles to the controller manager
  cm_->add_controller(
    test_controller, test_controller::TEST_CONTROLLER_NAME,
    test_controller::TEST_CONTROLLER_CLASS_NAME);
  cm_->add_controller(
    test_controller_2, TEST_CONTROLLER_2_NAME, test_controller::TEST_CONTROLLER_CLASS_NAME);
  cm_->add_controller(
    test_controller_3, TEST_CONTROLLER_3_NAME, test_controller::TEST_CONTROLLER_CLASS_NAME);

  EXPECT_EQ(3u, cm_->get_loaded_controllers().size());

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));

  // configure controller
  cm_->configure_controller(test_controller::TEST_CONTROLLER_NAME);
  cm_->configure_controller(TEST_CONTROLLER_2_NAME);
  cm_->configure_controller(TEST_CONTROLLER_3_NAME);

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_EQ(0u, test_controller->internal_counter) << "Controller is not started";
  EXPECT_EQ(0u, test_controller_2->internal_counter) << "Controller is not started";
  EXPECT_EQ(0u, test_controller_3->internal_counter) << "Controller is not started";

  // Start test_controller, no conflicts expected
  std::vector<std::string> start_controllers = {test_controller::TEST_CONTROLLER_NAME};
  std::vector<std::string> stop_controllers = {};
  auto switch_future = std::async(
    std::launch::async, &controller_manager::ControllerManager::switch_controller, cm_,
    start_controllers, stop_controllers, test_param.strictness, true, rclcpp::Duration(0, 0));

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_EQ(0u, test_controller->internal_counter) << "Controller is started at the end of update";
  {
    ControllerManagerRunner cm_runner(this);
    EXPECT_EQ(test_param.expected_return, switch_future.get());
  }
  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));

  // Start test_controller_2 and test_controller_3
  // BEST_EFFORT: controller_2 can be started
  // STRICT: controller_2 can NOT be started
  start_controllers = {TEST_CONTROLLER_2_NAME, TEST_CONTROLLER_3_NAME};
  stop_controllers = {};
  switch_future = std::async(
    std::launch::async, &controller_manager::ControllerManager::switch_controller, cm_,
    start_controllers, stop_controllers, test_param.strictness, true, rclcpp::Duration(0, 0));

  ASSERT_EQ(std::future_status::timeout, switch_future.wait_for(std::chrono::milliseconds(100)))
    << "switch_controller should be blocking until next update cycle";

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_EQ(0u, test_controller_2->internal_counter)
    << "Controller is started at the end of update";
  {
    ControllerManagerRunner cm_runner(this);
    EXPECT_EQ(controller_interface::return_type::OK, switch_future.get());
  }

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_GE(test_controller->internal_counter, 1u);

  // Should we expect two active controllers in best_effort?
  unsigned int active_controllers_count = 0;
  if (test_controller->get_state().id() == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE)
    ++active_controllers_count;

  if (test_controller_2->get_state().id() == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE)
    ++active_controllers_count;

  EXPECT_EQ(active_controllers_count, test_param.expected_active_controllers)
    << "The number of active controllers should be: " << test_param.expected_active_controllers;
}

TEST_P(TestControllerManager, per_controller_update_rate)
{
  auto strictness = GetParam().strictness;
  auto test_controller = std::make_shared<test_controller::TestController>();
  cm_->add_controller(
    test_controller, test_controller::TEST_CONTROLLER_NAME,
    test_controller::TEST_CONTROLLER_CLASS_NAME);
  EXPECT_EQ(1u, cm_->get_loaded_controllers().size());
  EXPECT_EQ(2, test_controller.use_count());

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_EQ(0u, test_controller->internal_counter)
    << "Update should not reach an unconfigured controller";

  EXPECT_EQ(
    lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, test_controller->get_state().id());

  test_controller->get_node()->set_parameter({"update_rate", 4});
  // configure controller
  cm_->configure_controller(test_controller::TEST_CONTROLLER_NAME);
  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_EQ(0u, test_controller->internal_counter) << "Controller is not started";

  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, test_controller->get_state().id());

  // Start controller, will take effect at the end of the update function
  std::vector<std::string> start_controllers = {test_controller::TEST_CONTROLLER_NAME};
  std::vector<std::string> stop_controllers = {};
  auto switch_future = std::async(
    std::launch::async, &controller_manager::ControllerManager::switch_controller, cm_,
    start_controllers, stop_controllers, strictness, true, rclcpp::Duration(0, 0));

  ASSERT_EQ(std::future_status::timeout, switch_future.wait_for(std::chrono::milliseconds(100)))
    << "switch_controller should be blocking until next update cycle";

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_EQ(0u, test_controller->internal_counter) << "Controller is started at the end of update";
  {
    ControllerManagerRunner cm_runner(this);
    EXPECT_EQ(controller_interface::return_type::OK, switch_future.get());
  }

  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, test_controller->get_state().id());

  EXPECT_EQ(
    controller_interface::return_type::OK,
    cm_->update(rclcpp::Time(0), rclcpp::Duration::from_seconds(0.01)));
  EXPECT_GE(test_controller->internal_counter, 1u);
  EXPECT_EQ(test_controller->get_update_rate(), 4u);
}

Strictness strict_config{STRICT, controller_interface::return_type::ERROR, 0u, 1u};
Strictness best_effort_config{BEST_EFFORT, controller_interface::return_type::OK, 1u, 2u};

INSTANTIATE_TEST_SUITE_P(
  test_strict_best_effort, TestControllerManager,
  testing::Values(strict_config, best_effort_config));
