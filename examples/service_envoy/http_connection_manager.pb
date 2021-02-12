codec_type: AUTO

stat_prefix: "ingress_http"

route_config {
  virtual_hosts {
    name: "service"
    domains: "*"
    routes {
      match {
        prefix: "/service"
      }
      route {
        cluster: "local_service"
        timeout {
          seconds: 0
        }
      }
    }
  }
}

http_filters {
  name: "router"
}
