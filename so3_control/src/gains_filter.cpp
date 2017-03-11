// Standard C++
#include <math.h>
#include <limits>
#include <map>
#include <string>
#include <iterator>

// ROS Related
#include <ros/ros.h>
#include <dynamic_reconfigure/DoubleParameter.h>
#include <dynamic_reconfigure/Reconfigure.h>
#include <dynamic_reconfigure/Config.h>

// quadrotor_control
// #include <so3_control/SO3Config.h>

class Gain
{
  public:

    Gain() :
      max_diff_(std::numeric_limits<double>::max()),
      latest_(0.0),
      target_(0.0),
      is_changing_(false)
    {
    }

    void set_max_diff(double max_diff) { max_diff_ = std::abs(max_diff); }

    void set_latest(double latest)
    {
      latest_ = latest;
      target_ = latest;
      is_changing_ = false;
    }

    void set_target(double target)
    {
      target_ = target;
      is_changing_ = (target_ != latest_);
    }

    double update()
    {
      if (!is_changing_)
        return latest_;

      double diff = target_ - latest_;
      if (std::abs(diff) > max_diff_)
      {
        double sign = (diff > 0) - (diff < 0);
        latest_ += sign * max_diff_;
      }
      else
      {
        is_changing_ = false;
        latest_ = target_;
      }
      return latest_;
    }
  
    bool target_achieved() { return !is_changing_; }

  private:
      double max_diff_, latest_, target_;
      bool is_changing_;
};

class GainsFilter 
{
  public:

    GainsFilter(ros::NodeHandle n, double rate);
    
    bool server_cb(dynamic_reconfigure::ReconfigureRequest &req, dynamic_reconfigure::ReconfigureResponse &res);

    void update();

  private:

    ros::NodeHandle n_;
    double rate_;

    std::map <std::string, Gain> Gains;

    // Services
    ros::ServiceServer server_;
    // ros::ServiceClient client_;
};

GainsFilter::GainsFilter(ros::NodeHandle nh, double rate) : n_(nh), rate_(rate)
{
  Gain g;

  g.set_max_diff(1.5 / rate_);
  Gains["kp_x"] = g;
  Gains["kp_y"] = g;
  Gains["kp_z"] = g;
  
  g.set_max_diff(1.5 / rate_);
  Gains["kd_x"] = g;
  Gains["kd_y"] = g;
  Gains["kd_z"] = g;
  
  g.set_max_diff(0.001 / rate_);
  Gains["ki_x"] = g;
  Gains["ki_y"] = g;
  Gains["ki_z"] = g;
 
  g.set_max_diff(0.001 / rate_);
  Gains["kib_x"] = g;
  Gains["kib_y"] = g;
  Gains["kib_z"] = g;

  // Service server
  server_ = n_.advertiseService("set_parameters", &GainsFilter::server_cb, this);

  // Service client
  // client_ = n_.serviceClient<dynamic_reconfigure::Reconfigure>("so3_control/set_parameters");
  // client_.waitForExistence();
}

// bool GainsFilter::server_cb(std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
// bool GainsFilter::server_cb()
bool GainsFilter::server_cb(dynamic_reconfigure::ReconfigureRequest &req, dynamic_reconfigure::ReconfigureResponse &res)
{
  // This works:
  dynamic_reconfigure::ReconfigureRequest srv_req;
  dynamic_reconfigure::ReconfigureResponse srv_resp;
  dynamic_reconfigure::Config conf;
  srv_req.config = conf;
  ros::service::call("so3_control/set_parameters", srv_req, srv_resp);
  // ROS_INFO_STREAM("size: " << srv_resp.config.doubles.size());

  // But for some reason, this doesn't:
  /*
  dynamic_reconfigure::Reconfigure srv;
  dynamic_reconfigure::Config conf;
  srv.request.config = conf;
  ROS_INFO_STREAM("Service call: " << client_.call(srv));
  ROS_INFO_STREAM("size: " << srv.response.config.doubles.size());
  */

  // Get the latest values
  for (unsigned int i=0; i < srv_resp.config.doubles.size(); i++)
  {
    std::string name = srv_resp.config.doubles[i].name;
    double value = srv_resp.config.doubles[i].value;
 
    auto it = Gains.find(name);
    if (it != Gains.end())
      it->second.set_latest(value);
    else
      ROS_DEBUG_STREAM("Gain " << name << " is ignored");
  }

  // Set the targets
  for (unsigned int i=0; i < req.config.doubles.size(); i++)
  {
    std::string name = req.config.doubles[i].name;
    double value = req.config.doubles[i].value;

    auto it = Gains.find(name);
    if (it != Gains.end())
    {
      it->second.set_target(value);
      ROS_INFO_STREAM("Setting target for " << name);
    }
    else
      ROS_WARN_STREAM("Gain " << name.c_str() << " not allowed");
  }
  return true;
}

void GainsFilter::update()
{
  dynamic_reconfigure::DoubleParameter double_param;
  dynamic_reconfigure::Config conf;

  auto it = Gains.begin();
  while(it != Gains.end())
  {
    if (!it->second.target_achieved())
    {
      double_param.name = it->first;
      double_param.value = it->second.update();
      conf.doubles.push_back(double_param);
    }
    it++;
  } 

  // If we have updates, then send 'em!
  if (conf.doubles.size() != 0)
  {
    dynamic_reconfigure::ReconfigureRequest srv_req;
    dynamic_reconfigure::ReconfigureResponse srv_resp;
    srv_req.config = conf;
    ros::service::call("so3_control/set_parameters", srv_req, srv_resp);
  }
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "gains_filter");
  ros::NodeHandle nh;

  double rate = 5;
  GainsFilter gf(nh, rate);

  ros::Rate r(rate); // Hz
  while (ros::ok())
  {
    ros::spinOnce();
    gf.update();
    r.sleep();
  }
  return 0;
}
