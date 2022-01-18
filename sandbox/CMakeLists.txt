cmake_minimum_required(VERSION 3.8)
project(sandbox)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(ping_pong src/ping_pong.cpp src/ping_node.cpp src/pong_node.cpp)
target_include_directories(ping_pong PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
ament_target_dependencies(ping_pong rclcpp std_msgs)
target_compile_features(ping_pong PUBLIC c_std_99 cxx_std_17)  # Require C99 and C++17

add_executable(empty_callbacks src/empty_callbacks.cpp)
target_include_directories(empty_callbacks PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
ament_target_dependencies(empty_callbacks rclcpp std_msgs)
target_compile_features(empty_callbacks PUBLIC c_std_99 cxx_std_17)  # Require C99 and C++17

add_executable(shared_nodes src/shared_nodes.cpp)
target_include_directories(shared_nodes PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
ament_target_dependencies(shared_nodes rclcpp std_msgs)
target_compile_features(shared_nodes PUBLIC c_std_99 cxx_std_17)  # Require C99 and C++17

add_executable(many_subs_per_node src/many_subs_per_node.cpp src/explosion_node.cpp)
target_include_directories(many_subs_per_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
ament_target_dependencies(many_subs_per_node rclcpp std_msgs)
target_compile_features(many_subs_per_node PUBLIC c_std_99 cxx_std_17)  # Require C99 and C++17

install(TARGETS ping_pong empty_callbacks shared_nodes many_subs_per_node
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # uncomment the line when a copyright and license is not present in all source files
  #set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # uncomment the line when this package is not in a git repo
  #set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
