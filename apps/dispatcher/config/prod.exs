# This file is responsible for configuring your application
# and its dependencies with the aid of the Mix.Config module.
use Mix.Config

config :dispatcher, Dispatcher.Application,
  build_env: "#{Mix.env}"

config :dispatcher, Dispatcher.InboundMessage,
  log_reading: false,
  startup_delay_ms: 5000,
  periodic_log_first_ms: (60 * 60 * 1000),
  periodic_log_ms: (120 * 60 * 1000),
  rpt_feed: "prod/mcr/f/report",
  cmd_feed: "prod/mcr/f/command",
  temperature_msgs: {Mcp.Sensor, :external_update},
  switch_msgs: {Mcp.Switch, :external_update}