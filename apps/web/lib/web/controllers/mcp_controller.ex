defmodule Web.McpController do
  @moduledoc """
  """
  use Timex
  use Web, :controller

  alias Mcp.DevAlias
  alias Mcp.Sensor
  alias Mcp.Switch

  def index(conn, _params) do
    all_fnames = DevAlias.all(:friendly_names) |> MapSet.new()
    switch_fnames = Switch.all(:friendly_names)
    sensor_fnames = Sensor.all(:friendly_names)

    known_fnames = (switch_fnames ++ sensor_fnames) |> MapSet.new()
    unknown_fnames =
      MapSet.difference(all_fnames, known_fnames) |> Enum.sort()

    unknown = Enum.map(unknown_fnames, fn(x) -> dev_alias_details(x) end)
    sensors = Enum.map(sensor_fnames, fn(x) -> sensor_details(x) end)

    render conn, "index.html",
      all_fnames_count: Enum.count(all_fnames),
      switch_fnames: switch_fnames,
      switch_fnames_count: Enum.count(switch_fnames),
      sensor_fnames: sensor_fnames,
      sensor_fnames_count: Enum.count(sensor_fnames),
      unknown_fnames: unknown_fnames,
      unknown_fnames_count: Enum.count(unknown_fnames),
      unknown: unknown
  end

  def show(conn, %{"fname" => fname}) do

    %DevAlias{device: device,
              last_seen_at: last_seen,
              inserted_at: inserted_at} = DevAlias.get_by_friendly_name(fname)

    last_seen_secs = Timex.diff(Timex.now(), last_seen, :seconds)

    if Switch.is_switch?(fname) do
      render conn, "switch.html",
        fname: fname, device: device, last_seen: last_seen,
        last_seen_secs: last_seen_secs,
        first_seen: inserted_at,
        state: Switch.get_state(fname)
    else
      render conn, "sensor.html",
        fname: fname, device: device, last_seen: last_seen,
        last_seen_secs: last_seen_secs,
        first_seen: inserted_at,
        fahrenheit: Sensor.fahrenheit(fname),
        relhum: Sensor.relhum(fname)

    end
  end

  defp dev_alias_details(fname) do
    a = DevAlias.get_by_friendly_name(fname)
    %{device: a.device, fname: a.friendly_name, desc: a.description,
      last_seen_at: Timex.format!(a.last_seen_at, "{ISO:Extended:Z}")}
  end

  defp sensor_details(fname) do
    a = DevAlias.get_by_friendly_name(fname)

    last_seen_secs = Timex.diff(Timex.now(), a.last_seen_at, :seconds)

    s = Sensor.get(:friendly_name, fname)
    %{device: s.device, fname: fname, desc: a.description,
      type: s.sensor_type,
      reading_at: s.reading_at, last_seen_at: s.last_seen_at,
      last_seen_secs: last_seen_secs}
  end
end
