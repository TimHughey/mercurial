# This file is responsible for configuring your application
# and its dependencies with the aid of the Mix.Config module.
use Mix.Config

config :logger, level: :info
# level: :warn
# level: :info

config :mcp,
  feeds: [
    cmd: {"test/mcr/f/command", :qos0},
    rpt: {"test/mcr/f/report", :qos0},
    ota: {"test/mcr/f/ota", :qos0}
  ]

config :mcp, Mcp.Dutycycle, routine_check_ms: 1000

config :mcp, Janitor,
  switch_cmds: [purge: true, interval_mins: 2, older_than_hrs: 24 * 7, log: true],
  orphan_acks: [interval_mins: 1, older_than_mins: 1, log: true]

config :mcp, MessageSave,
  save: true,
  delete: [all_at_startup: true, older_than_hrs: 12]

config :mcp, Mqtt.Client,
  log_dropped_msg: true,
  broker: [
    host: 'jophiel.wisslanding.com',
    port: 1883,
    client_id: "janice-test",
    clean_sess: true,
    username: "mqtt",
    password: "mqtt",
    auto_resub: true,
    reconnect: 2
  ],
  timesync: [frequency: 5 * 1000, loops: 5, forever: false, log: false]

config :mcp, Mqtt.InboundMessage,
  log_reading: true,
  startup_delay_ms: 200,
  periodic_log_first_ms: 30_000,
  periodic_log_ms: 10 * 60 * 1000

config :mcp, Fact.Influx,
  database: "jan_test",
  host: "jophiel.wisslanding.com",
  auth: [method: :basic, username: "jan_test", password: "jan_test"],
  http_opts: [insecure: true],
  pool: [max_overflow: 10, size: 10, timeout: 60_000, max_connections: 30],
  port: 8086,
  scheme: "http",
  writer: Instream.Writer.Line

config :mcp, Mixtank.Control, control_temp_ms: 100

config :mcp, Repo,
  adapter: Ecto.Adapters.Postgres,
  migration_timestamps: [:utc_datetime_usec],
  username: "jan_test",
  password: "jan_test",
  database: "jan_test",
  hostname: "standby.db.wisslanding.com",
  pool_size: 10

config :mcp, Janice.Scheduler,
  jobs: [
    # Every minute
    {"* * * * *", {Janice.Jobs, :touch_file, ["/tmp/janice-file"]}}
    # Every 15 minutes
    # {"*/15 * * * *",   fn -> System.cmd("rm", ["/tmp/tmp_"]) end},
    # Runs on 18, 20, 22, 0, 2, 4, 6:
    # {"0 18-6/2 * * *", fn -> :mnesia.backup('/var/backup/mnesia') end},
    # Runs every midnight:
    # {"@daily",         {Backup, :backup, []}}
  ]

config :mcp, Mcp.SoakTest,
  startup_delay_ms: 0,
  periodic_log_first_ms: 1 * 60 * 1000,
  periodic_log_ms: 15 * 50 * 1000,
  flash_led_ms: 1000

config :mcp, Switch, logCmdAck: false
