
#include <pluginlib/class_loader.h>
#include <ros/ros.h>
// MoveIt
#include <moveit/kinematic_constraints/utils.h>
#include <moveit/plan_execution/plan_execution.h>
#include <moveit/planning_interface/planning_interface.h>
#include <moveit/planning_pipeline/planning_pipeline.h>
#include <moveit/planning_scene_monitor/current_state_monitor.h>
#include <moveit/planning_scene_monitor/planning_scene_monitor.h>
#include <moveit/robot_model_loader/robot_model_loader.h>
#include <moveit/robot_state/conversions.h>
#include <moveit/trajectory_execution_manager/trajectory_execution_manager.h>
#include <moveit_msgs/DisplayTrajectory.h>
#include <moveit_msgs/PlanningScene.h>
#include <moveit_visual_tools/moveit_visual_tools.h>

constexpr char LOGNAME[] = "pick_and_place_node";

void printPlannerConfigMap(
    const planning_interface::PlannerConfigurationMap& planner_config_map) {
  for (auto config : planner_config_map) {
    ROS_INFO_NAMED(LOGNAME, "Map Name: %s", config.first.c_str());
    ROS_INFO_NAMED(LOGNAME, "\tGroup: %s", config.second.group.c_str());
    ROS_INFO_NAMED(LOGNAME, "\tName: %s", config.second.name.c_str());

    for (auto setting : config.second.config) {
      ROS_INFO_NAMED(LOGNAME, "\t\tSetting: %s", setting.first.c_str());
      ROS_INFO_NAMED(LOGNAME, "\t\tValue: %s", setting.second.c_str());
    }
  }
}

void printJointTrajectory(
    const trajectory_msgs::JointTrajectory& joint_trajectory) {
  ROS_INFO_NAMED(LOGNAME, "Num joints: %ld",
                 joint_trajectory.joint_names.size());
  ROS_INFO_NAMED(LOGNAME, "Num points: %ld", joint_trajectory.points.size());

  for (int i = 0; i < joint_trajectory.joint_names.size(); i++) {
    std::string name = joint_trajectory.joint_names[i];
    std::cout << name << " ";
  }

  for (int i = 0; i < joint_trajectory.points.size(); i++) {
    trajectory_msgs::JointTrajectoryPoint traj_point =
        joint_trajectory.points[i];
    std::cout << "\npositions" << std::endl;
    for (int k = 0; k < traj_point.positions.size(); k++) {
      double position = traj_point.positions[k];
      std::cout << position << " ";
    }

    std::cout << "\nvelocities" << std::endl;
    for (int k = 0; k < traj_point.velocities.size(); k++) {
      double velocity = traj_point.velocities[k];
      std::cout << velocity << " ";
    }

    std::cout << "\naccelerations" << std::endl;
    for (int k = 0; k < traj_point.accelerations.size(); k++) {
      double accel = traj_point.accelerations[k];
      std::cout << accel << " ";
    }

    std::cout << "\neffort" << std::endl;
    for (int k = 0; k < traj_point.effort.size(); k++) {
      double effort = traj_point.effort[k];
      std::cout << effort << " ";
    }
  }
}

void printControllers(
    const moveit_controller_manager::MoveItControllerManagerPtr&
        controller_manager) {
  std::vector<std::string> active_controllers;
  controller_manager->getActiveControllers(active_controllers);
  std::vector<std::string> known_controllers;
  controller_manager->getControllersList(known_controllers);

  ROS_INFO_NAMED(LOGNAME, "Active controllers: %ld", active_controllers.size());
  for (auto controller : active_controllers) {
    std::cout << controller << ", " << std::endl;
  }

  ROS_INFO_NAMED(LOGNAME, "Known controllers: %ld", known_controllers.size());
  for (auto controller : known_controllers) {
    std::cout << controller << ", " << std::endl;
  }
}

void promptAnyInput() {
  std::cout << std::endl;
  std::cout << "Press any key to continue: ";
  getchar();
}

void addPlannerConfigurationSettings(
    planning_interface::PlannerConfigurationMap& planner_config_map,
    const std::string& group_name,
    const std::map<std::string, std::string>& setting_map) {
  planning_interface::PlannerConfigurationSettings planner_settings;
  planner_settings.group = group_name;
  planner_settings.name = setting_map.at("type");
  planner_settings.config = setting_map;
  std::string key = group_name + "[" + setting_map.at("type") + "]";
  planner_config_map[key] = planner_settings;
}

int main(int argc, char** argv) {
  ros::init(argc, argv, "pick_and_place_node");
  ros::AsyncSpinner spinner(1);
  spinner.start();
  ros::NodeHandle node_handle("~");

  ROS_DEBUG_NAMED(LOGNAME, "Start!");

  // Initialization
  // ^^^^^^^^^^^^^^
  // ^^^^^^^^^^^^^^
  robot_model_loader::RobotModelLoaderPtr robot_model_loader(
      new robot_model_loader::RobotModelLoader("robot_description"));

  planning_scene_monitor::PlanningSceneMonitorPtr psm(
      new planning_scene_monitor::PlanningSceneMonitor(robot_model_loader));

  /* listen for planning scene messages on topic /XXX and apply them to
                       the internal planning scene accordingly */
  psm->startSceneMonitor();
  /* listens to changes of world geometry, collision objects, and (optionally)
   * octomaps */
  psm->startWorldGeometryMonitor();
  /* listen to joint state updates as well as changes in attached collision
     objects and update the internal planning scene accordingly*/
  psm->startStateMonitor();

  psm->updateSceneWithCurrentState();

  /* We can also use the RobotModelLoader to get a robot model which contains
   * the robot's kinematic information */
  moveit::core::RobotModelPtr robot_model = robot_model_loader->getModel();

  /* We can get the most up to date robot state from the PlanningSceneMonitor by
     locking the internal planning scene for reading. This lock ensures that the
     underlying scene isn't updated while we are reading it's state.
     RobotState's are useful for computing the forward and inverse kinematics of
     the robot among many other uses */
  // moveit::core::RobotStatePtr robot_state(new moveit::core::RobotState(
  //     planning_scene_monitor::LockedPlanningSceneRO(psm)->getCurrentState()));

  planning_scene_monitor::CurrentStateMonitorPtr csm = psm->getStateMonitor();

  moveit::core::RobotStatePtr robot_state = csm->getCurrentState();

  /* Create a JointModelGroup to keep track of the current robot pose and
     planning group. The Joint Model group is useful for dealing with one set of
     joints at a time such as a left arm or a end effector */
  const std::string group_name = "panda_arm";
  const moveit::core::JointModelGroup* joint_model_group =
      robot_state->getJointModelGroup(group_name);

  // We can now setup the PlanningPipeline object, which will use the ROS
  // parameter server to determine the set of request adapters and the planning
  // plugin to use
  planning_pipeline::PlanningPipelinePtr planning_pipeline(
      new planning_pipeline::PlanningPipeline(
          robot_model, node_handle, "planning_plugin", "request_adapters"));

  const planning_interface::PlannerManagerPtr planner_manager =
      planning_pipeline->getPlannerManager();

  // Pose Goal
  // ^^^^^^^^^
  // ^^^^^^^^^
  // We will now create a motion plan request for the right arm of the Panda
  // specifying the desired pose of the end-effector as input.
  planning_interface::MotionPlanRequest req;
  planning_interface::MotionPlanResponse res;

  robot_state->printStatePositions();

  // robot_state->zeroVelocities();
  // robot_state->zeroAccelerations();
  // robot_state->zeroEffort();

  // std_msgs::Header header;
  // header.stamp = ros::Time::now();
  // req.start_state.joint_state.header = header;

  // std::vector<std::string> names = robot_model->getVariableNames();
  // std::size_t num_joints = robot_model->getVariableCount();
  // double* positions = robot_state->getVariablePositions();
  // double* velocities = robot_state->getVariableVelocities();
  // double* efforts = robot_state->getVariableEffort();
  // for (std::size_t i = 0; i < num_joints; ++i) {
  //   req.start_state.joint_state.name.push_back(names[i]);
  //   req.start_state.joint_state.position.push_back(positions[i]);
  //   req.start_state.joint_state.velocity.push_back(velocities[i]);
  //   req.start_state.joint_state.effort.push_back(efforts[i]);
  // }

  geometry_msgs::PoseStamped pose;
  pose.header.frame_id = "panda_link0";
  pose.pose.position.x = 0.5;
  pose.pose.position.y = 0.0;
  pose.pose.position.z = 0.75;
  pose.pose.orientation.w = 1.0;
  pose.pose.orientation.x = 0.0;
  pose.pose.orientation.y = 0.0;
  pose.pose.orientation.z = 0.0;

  // A tolerance of 0.01 m is specified in position
  // and 0.01 radians in orientation
  std::vector<double> tolerance_pose(3, 0.01);
  std::vector<double> tolerance_angle(3, 0.01);

  req.group_name = group_name;
  req.allowed_planning_time = 5.0;
  req.planner_id = "panda_arm[EST]";

  moveit_msgs::Constraints pose_goal =
      kinematic_constraints::constructGoalConstraints(
          "panda_link8", pose, tolerance_pose, tolerance_angle);
  req.goal_constraints.push_back(pose_goal);

  // Before planning, we will need a Read Only lock on the planning scene so
  // that it does not modify the world representation while planning
  {
    planning_scene_monitor::LockedPlanningSceneRO lscene(psm);
    /* Now, call the pipeline and check whether planning was successful. */
    planning_pipeline->generatePlan(lscene, req, res);
  }
  /* Now, call the pipeline and check whether planning was successful. */
  /* Check that the planning was successful */
  if (res.error_code_.val != res.error_code_.SUCCESS) {
    ROS_ERROR("Could not compute plan successfully");
    return 0;
  }

  // Visualize the result
  // ^^^^^^^^^^^^^^^^^^^^
  // ^^^^^^^^^^^^^^^^^^^^
  // The package MoveItVisualTools provides many capabilities for visualizing
  // objects, robots, and trajectories in RViz as well as debugging tools such
  // as step-by-step introspection of a script.
  namespace rvt = rviz_visual_tools;
  moveit_visual_tools::MoveItVisualTools visual_tools(
      "panda_link0", rviz_visual_tools::RVIZ_MARKER_TOPIC, psm);
  visual_tools.deleteAllMarkers();

  ros::Publisher display_publisher =
      node_handle.advertise<moveit_msgs::DisplayTrajectory>(
          "/move_group/display_planned_path", 1, true);
  moveit_msgs::DisplayTrajectory display_trajectory;

  /* Visualize the trajectory */
  ROS_INFO("Visualizing the trajectory");
  moveit_msgs::MotionPlanResponse response;
  res.getMessage(response);

  display_trajectory.trajectory_start = response.trajectory_start;
  display_trajectory.trajectory.push_back(response.trajectory);
  display_publisher.publish(display_trajectory);
  visual_tools.publishTrajectoryLine(display_trajectory.trajectory.back(),
                                     joint_model_group);
  visual_tools.trigger();

  // Execute Trajectory
  // ^^^^^^^^^^^^^^^^^^^^
  // ^^^^^^^^^^^^^^^^^^^^
  moveit_msgs::RobotTrajectory robot_trajectory = response.trajectory;
  trajectory_msgs::JointTrajectory joint_trajectory =
      robot_trajectory.joint_trajectory;

  trajectory_execution_manager::TrajectoryExecutionManagerPtr
      traj_execution_manager(
          new trajectory_execution_manager::TrajectoryExecutionManager(
              robot_model, csm));

  moveit_controller_manager::MoveItControllerManagerPtr controller_manager =
      traj_execution_manager->getControllerManager();

  printControllers(controller_manager);

  traj_execution_manager->push(robot_trajectory);
  moveit_controller_manager::ExecutionStatus status =
      traj_execution_manager->executeAndWait();

  ROS_INFO_NAMED(LOGNAME, "Status: %s", status.asString().c_str());

  promptAnyInput();

  std::cout << "Finished!" << std::endl;
  return 0;
}