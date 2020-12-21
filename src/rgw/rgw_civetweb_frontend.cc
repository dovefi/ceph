// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include <set>
#include <string>

#include <boost/utility/string_ref.hpp>

#include "rgw_frontend.h"
#include "rgw_client_io_filters.h"

#define dout_subsys ceph_subsys_rgw

static int civetweb_callback(struct mg_connection* conn)
{
  // 结构体定义 src/civetweb/include/civetweb.h:57
  // 获取具体的请求，req_info 的user_data 是 mg_start() 的时候传递给web服务器的
  // RGWCivetWebFrontend 实体
  const struct mg_request_info* const req_info = mg_get_request_info(conn);
  return static_cast<RGWCivetWebFrontend *>(req_info->user_data)->process(conn);
}

// http 请求真正的处理函数入口
int RGWCivetWebFrontend::process(struct mg_connection*  const conn)
{
  /* Hold a read lock over access to env.store for reconfiguration. */
  RWLock::RLocker lock(env.mutex);

  RGWCivetWeb cw_client(conn);
  // 给RGWCivetWeb 加上各种过滤器
  // 返回一个 src/rgw/rgw_client_io.h:88
  /*
   *                                                      --> ConlenControllingFilter   ④
   *                                                      --> ChunkingFilter            ③
   * BasicClient-->RestFulClient-->DecoratedRestfulClient --> BufferingFilter           ②
   *                                                      --> RecorderingFilter         ①
   * 通过模板类，各filter实现不桶的方法，调用顺序是 ①，②，③，④
   * */
  auto real_client_io = rgw::io::add_reordering(
                          rgw::io::add_buffering(dout_context,
                            rgw::io::add_chunking(
                              rgw::io::add_conlen_controlling(
                                &cw_client))));
  RGWRestfulIO client_io(dout_context, &real_client_io);

  // 创建request 请求体
  RGWRequest req(env.store->get_new_req_id());
  int http_ret = 0;
  int ret = process_request(env.store, env.rest, &req, env.uri_prefix,
                            *env.auth_registry, &client_io, env.olog, &http_ret);
  if (ret < 0) {
    /* We don't really care about return code. */
    dout(20) << "process_request() returned " << ret << dendl;
  }

  if (http_ret <= 0) {
    /* Mark as processed. */
    return 1;
  }

  return http_ret;
}

// 启动civeWeb http 网关
int RGWCivetWebFrontend::run()
{
  auto& conf_map = conf->get_config_map();

  set_conf_default(conf_map, "num_threads",
                   std::to_string(g_conf->rgw_thread_pool_size));
  set_conf_default(conf_map, "decode_url", "no");
  set_conf_default(conf_map, "enable_keep_alive", "yes");
  set_conf_default(conf_map, "validate_http_method", "no");
  set_conf_default(conf_map, "canonicalize_url_path", "no");
  set_conf_default(conf_map, "enable_auth_domain_check", "no");

  std::string listening_ports;
  // support multiple port= entries
  auto range = conf_map.equal_range("port");
  for (auto p = range.first; p != range.second; ++p) {
    std::string port_str = p->second;
    // support port= entries with multiple values
    std::replace(port_str.begin(), port_str.end(), '+', ',');
    if (!listening_ports.empty()) {
      listening_ports.append(1, ',');
    }
    listening_ports.append(port_str);
  }
  if (listening_ports.empty()) {
    listening_ports = "80";
  }
  conf_map.emplace("listening_ports", std::move(listening_ports));

  /* Set run_as_user. This will cause civetweb to invoke setuid() and setgid()
   * based on pw_uid and pw_gid obtained from pw_name. */
  std::string uid_string = g_ceph_context->get_set_uid_string();
  if (! uid_string.empty()) {
    conf_map.emplace("run_as_user", std::move(uid_string));
  }

  /* Prepare options for CivetWeb. */
  const std::set<boost::string_ref> rgw_opts = { "port", "prefix" };

  std::vector<const char*> options;

  for (const auto& pair : conf_map) {
    if (! rgw_opts.count(pair.first)) {
      /* CivetWeb doesn't understand configurables of the glue layer between
       * it and RadosGW. We need to strip them out. Otherwise CivetWeb would
       * signalise an error. */
      options.push_back(pair.first.c_str());
      options.push_back(pair.second.c_str());

      dout(20) << "civetweb config: " << pair.first
               << ": " << pair.second << dendl;
    }
  }

  options.push_back(nullptr);
  /* Initialize the CivetWeb right now. */
  // 代码定义在 src/civetweb/include/civetweb.h:95
  struct mg_callbacks cb;
  memset((void *)&cb, 0, sizeof(cb));
  // 设置civetweb 接受到请求之后的回调函数
  // 当接收到请求之后，调用begin_request.
  cb.begin_request = civetweb_callback;
  cb.log_message = rgw_civetweb_log_callback;
  cb.log_access = rgw_civetweb_log_access_callback;
  // 启动web 服务器，定义 src/civetweb/include/civetweb.h:254
  // 其中 this 是将当前 RGWCivetWebFrontend 指针传递给web服务器的 void* user_data
  // 用作回调。
  ctx = mg_start(&cb, this, options.data());

  return ! ctx ? -EIO : 0;
} /* RGWCivetWebFrontend::run */
