# DO NOT UPGRADE!
# Grafana 7.5.5 is the last Apache-2.0 licensed version.
# New versions are licensed under the AGPL, which is forbidden at Google.
FROM grafana/grafana:7.5.5
WORKDIR /app
COPY provisioning /etc/grafana/provisioning
COPY dashboards /var/lib/grafana/dashboards
COPY grafana.ini /etc/grafana/grafana.ini
EXPOSE 8080
