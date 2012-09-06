/*********************************************************************
* Software License Agreement (BSD License)
* 
*  Copyright (c) 2008, Willow Garage, Inc.
*  All rights reserved.
* 
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
* 
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
* 
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/


//Adapted version of image_view to display self_mask overlayed over camera image stream


#include <opencv/cv.h>
#include <opencv/highgui.h>

#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <cv_bridge/CvBridge.h>
#include <image_transport/image_transport.h>

#include <boost/thread.hpp>
#include <boost/format.hpp>

#ifdef HAVE_GTK
#include <gtk/gtk.h>

// Platform-specific workaround for #3026: image_view doesn't close when
// closing image window. On platforms using GTK+ we connect this to the
// window's "destroy" event so that image_view exits.
static void destroy(GtkWidget *widget, gpointer data)
{
  ros::shutdown();
}
#endif

class ImageView
{
private:
  image_transport::Subscriber sub_;
  
  sensor_msgs::ImageConstPtr last_msg_;
  sensor_msgs::CvBridge img_bridge_;
  boost::mutex image_mutex_;
  
  std::string window_name_;
  boost::format filename_format_;
  int count_;

public:
  ImageView(const ros::NodeHandle& nh, const std::string& transport)
    : filename_format_(""), count_(0)
  {
    std::string topic = nh.resolveName("image");
    ros::NodeHandle local_nh("~");
    local_nh.param("window_name", window_name_, topic);

    bool autosize;
    local_nh.param("autosize", autosize, false);
    
    std::string format_string;
    local_nh.param("filename_format", format_string, std::string("frame%04i.jpg"));
    filename_format_.parse(format_string);

    const char* name = window_name_.c_str();
    cvNamedWindow(name, autosize ? CV_WINDOW_AUTOSIZE : 0);
    cvSetMouseCallback(name, &ImageView::mouse_cb, this);
#ifdef HAVE_GTK
    GtkWidget *widget = GTK_WIDGET( cvGetWindowHandle(name) );
    g_signal_connect(widget, "destroy", G_CALLBACK(destroy), NULL);
#endif
    cvStartWindowThread();

    image_transport::ImageTransport it(nh);
    sub_ = it.subscribe(topic, 1, &ImageView::image_cb, this, transport);
  }

  ~ImageView()
  {
    cvDestroyWindow(window_name_.c_str());
  }

  void image_cb(const sensor_msgs::ImageConstPtr& msg)
  {
    boost::lock_guard<boost::mutex> guard(image_mutex_);
    
    // Hang on to message pointer for sake of mouse_cb
    last_msg_ = msg;

    // May want to view raw bayer data
    // NB: This is hacky, but should be OK since we have only one image CB.
    if (msg->encoding.find("bayer") != std::string::npos)
      boost::const_pointer_cast<sensor_msgs::Image>(msg)->encoding = "mono8";

    if (img_bridge_.fromImage(*msg, "bgra8")){
    	IplImage* ipl_image = img_bridge_.toIpl();
    	ROS_INFO("received image with %d channels", ipl_image->nChannels);

    	IplImage* alpha = cvCreateImage(cvGetSize(ipl_image), IPL_DEPTH_8U, 1);
    	IplImage* b = cvCreateImage(cvGetSize(ipl_image), IPL_DEPTH_8U, 1);
    	IplImage* g = cvCreateImage(cvGetSize(ipl_image), IPL_DEPTH_8U, 1);
    	IplImage* r = cvCreateImage(cvGetSize(ipl_image), IPL_DEPTH_8U, 1);
    	cvSplit(ipl_image, b,g,r,alpha);

    	double alpha_value = 0.7;
    	cvAddWeighted(b, alpha_value, alpha, 1.0 - alpha_value, 0.0, b);
    	cvAddWeighted(g, alpha_value, alpha, 1.0 - alpha_value, 0.0, g);
    	cvAddWeighted(r, alpha_value, alpha, 1.0 - alpha_value, 0.0, r);

    	cvMerge(b,g,r,NULL, ipl_image);

    	cvShowImage(window_name_.c_str(),ipl_image);
    	cvReleaseImage(&alpha);
    	cvReleaseImage(&b);
    	cvReleaseImage(&g);
    	cvReleaseImage(&r);
    }
    else
      ROS_ERROR("Unable to convert %s image to bgr8", msg->encoding.c_str());
  }

  static void mouse_cb(int event, int x, int y, int flags, void* param)
  {
    if (event != CV_EVENT_LBUTTONDOWN)
      return;
    
    ImageView *iv = (ImageView*)param;
    boost::lock_guard<boost::mutex> guard(iv->image_mutex_);

    IplImage *image = iv->img_bridge_.toIpl();
    if (image) {
      std::string filename = (iv->filename_format_ % iv->count_).str();
      cvSaveImage(filename.c_str(), image);
      ROS_INFO("Saved image %s", filename.c_str());
      iv->count_++;
    } else {
      ROS_WARN("Couldn't save image, no data!");
    }
  }
};

int main(int argc, char **argv)
{
  ros::init(argc, argv, "image_view", ros::init_options::AnonymousName);
  ros::NodeHandle n;
  if (n.resolveName("image") == "/image") {
    ROS_WARN("image_view: image has not been remapped! Typical command-line usage:\n"
             "\t$ ./image_view image:=<image topic> [transport]");
  }

  ImageView view(n, (argc > 1) ? argv[1] : "raw");

  ros::spin();
  
  return 0;
}
