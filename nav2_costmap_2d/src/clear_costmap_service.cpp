// Copyright (c) 2018 Intel Corporation
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

#include <vector>
#include <string>
#include <algorithm>
#include <memory>

#include "nav2_costmap_2d/clear_costmap_service.hpp"
#include "nav2_costmap_2d/costmap_2d_ros.hpp"

namespace nav2_costmap_2d
{

using std::vector;
using std::string;
using std::shared_ptr;
using std::any_of;
using ClearExceptRegion = nav2_msgs::srv::ClearCostmapExceptRegion;
using ClearAroundRobot = nav2_msgs::srv::ClearCostmapAroundRobot;
using ClearEntirely = nav2_msgs::srv::ClearEntireCostmap;

ClearCostmapService::ClearCostmapService(
  nav2_util::LifecycleNode::SharedPtr node,
  Costmap2DROS & costmap)
: node_(node), costmap_(costmap)
{
  reset_value_ = costmap_.getCostmap()->getDefaultValue();

  node_->get_parameter("clearable_layers", clearable_layers_);

  clear_except_service_ = node_->create_service<ClearExceptRegion>(
    "clear_except_" + costmap_.getName(),
    std::bind(
      &ClearCostmapService::clearExceptRegionCallback, this,
      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

  clear_around_service_ = node_->create_service<ClearAroundRobot>(
    "clear_around_" + costmap.getName(),
    std::bind(
      &ClearCostmapService::clearAroundRobotCallback, this,
      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

  clear_entire_service_ = node_->create_service<ClearEntirely>(
    "clear_entirely_" + costmap_.getName(),
    std::bind(
      &ClearCostmapService::clearEntireCallback, this,
      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void ClearCostmapService::clearExceptRegionCallback(
  const shared_ptr<rmw_request_id_t>/*request_header*/,
  const shared_ptr<ClearExceptRegion::Request> request,
  const shared_ptr<ClearExceptRegion::Response>/*response*/)
{
  // RCLCPP_INFO(
  //   node_->get_logger(),
  //   "Received request to clear except a region the " + costmap_.getName());

  clearExceptRegion(request->reset_distance);
}

void ClearCostmapService::clearAroundRobotCallback(
  const shared_ptr<rmw_request_id_t>/*request_header*/,
  const shared_ptr<ClearAroundRobot::Request> request,
  const shared_ptr<ClearAroundRobot::Response>/*response*/)
{
  // RCLCPP_INFO(
  //   node_->get_logger(),
  //   "Received request to clear around robot the " + costmap_.getName());

  if ((request->window_size_x == 0) || (request->window_size_y == 0)) {
    clearEntirely();
    return;
  }

  clearAroundRobot(request->window_size_x, request->window_size_y);
}

void ClearCostmapService::clearEntireCallback(
  const std::shared_ptr<rmw_request_id_t>/*request_header*/,
  const std::shared_ptr<ClearEntirely::Request>/*request*/,
  const std::shared_ptr<ClearEntirely::Response>/*response*/)
{
  // RCLCPP_INFO(node_->get_logger(), "Received request to clear entirely the " + costmap_.getName());

  clearEntirely();
}

void ClearCostmapService::clearExceptRegion(const double reset_distance)
{
  double x, y;

  if (!getPosition(x, y)) {
    RCLCPP_ERROR(node_->get_logger(), "Cannot clear map because robot pose cannot be retrieved.");
    return;
  }

  auto layers = costmap_.getLayeredCostmap()->getPlugins();

  for (auto & layer : *layers) {
    if (isClearable(getLayerName(*layer))) {
      auto costmap_layer = std::static_pointer_cast<CostmapLayer>(layer);
      clearLayerExceptRegion(costmap_layer, x, y, reset_distance);
    }
  }
}

void ClearCostmapService::clearAroundRobot(double window_size_x, double window_size_y)
{
  double pose_x, pose_y;

  if (!getPosition(pose_x, pose_y)) {
    RCLCPP_ERROR(node_->get_logger(), "Cannot clear map because robot pose cannot be retrieved.");
    return;
  }

  std::vector<geometry_msgs::msg::Point> clear_poly;
  geometry_msgs::msg::Point pt;

  pt.x = pose_x - window_size_x / 2;
  pt.y = pose_y - window_size_y / 2;
  clear_poly.push_back(pt);

  pt.x = pose_x + window_size_x / 2;
  pt.y = pose_y - window_size_y / 2;
  clear_poly.push_back(pt);

  pt.x = pose_x + window_size_x / 2;
  pt.y = pose_y + window_size_y / 2;
  clear_poly.push_back(pt);

  pt.x = pose_x - window_size_x / 2;
  pt.y = pose_y + window_size_y / 2;
  clear_poly.push_back(pt);

  costmap_.getCostmap()->setConvexPolygonCost(clear_poly, reset_value_);
}

void ClearCostmapService::clearEntirely()
{
  std::unique_lock<Costmap2D::mutex_t> lock(*(costmap_.getCostmap()->getMutex()));
  costmap_.resetLayers();
}

bool ClearCostmapService::isClearable(const string & layer_name) const
{
  return count(begin(clearable_layers_), end(clearable_layers_), layer_name) != 0;
}

void ClearCostmapService::clearLayerExceptRegion(
  std::shared_ptr<CostmapLayer> & costmap, double pose_x, double pose_y, double reset_distance)
{
  std::unique_lock<Costmap2D::mutex_t> lock(*(costmap->getMutex()));

  geometry_msgs::msg::PoseStamped pose;
  if (!costmap_.getRobotPose(pose)) {
    return;
  }
  const double yaw = tf2::getYaw(pose.pose.orientation);

  // 사각형의 꼭짓점 계산
  double half_dist = reset_distance / 2.0;
  std::vector<Point> corners = {
    {pose_x - half_dist, pose_y - half_dist},
    {pose_x + 0.259, pose_y - half_dist},
    {pose_x + 0.259, pose_y + half_dist},
    {pose_x - half_dist, pose_y + half_dist}
  };

  // 회전된 사각형의 꼭짓점 계산
  std::vector<Point> rotated_corners;
  for (const auto& corner : corners) {
    Point rotated_point = rotatePoint(pose_x, pose_y, yaw, corner);
    rotated_corners.push_back(rotated_point);
  }

  // 변환된 좌표를 맵 좌표로 변환
  std::vector<Point> map_corners;
  for (const auto& corner : rotated_corners) {
    int map_x, map_y;
    costmap->worldToMapEnforceBounds(corner.x, corner.y, map_x, map_y);
    map_corners.push_back({static_cast<double>(map_x), static_cast<double>(map_y)});
  }

  // 회전된 사각형의 꼭짓점을 clearArea 함수에 전달
  costmap->clearArea(map_corners);

  double ox = costmap->getOriginX(), oy = costmap->getOriginY();
  double width = costmap->getSizeInMetersX(), height = costmap->getSizeInMetersY();
  costmap->addExtraBounds(ox, oy, ox + width, oy + height);
}

bool ClearCostmapService::getPosition(double & x, double & y) const
{
  geometry_msgs::msg::PoseStamped pose;
  if (!costmap_.getRobotPose(pose)) {
    return false;
  }

  x = pose.pose.position.x;
  y = pose.pose.position.y;

  return true;
}

string ClearCostmapService::getLayerName(const Layer & layer) const
{
  string name = layer.getName();

  size_t slash = name.rfind('/');

  if (slash != std::string::npos) {
    name = name.substr(slash + 1);
  }

  return name;
}

}  // namespace nav2_costmap_2d
