address {
  socket_address {
    protocol: TCP
    port_value: 80
  }
}
filter_chains {
  filters {
    name: "http_connection_manager"
  }
}
