defmodule Mcp.SoakTest do
  @moduledoc false
  require Logger
  use GenServer
  import Application, only: [get_env: 2]
  import Process, only: [send_after: 3]

  import Janice.TimeSupport, only: [ms: 1]

  alias Fact.LedFlashes

  def start_link(s) do
    GenServer.start_link(Mcp.SoakTest, s, name: Mcp.SoakTest)
  end

  ## Callbacks

  def init(s)
      when is_map(s) do
    s = Map.put_new(s, :running, false)

    s =
      case Map.get(s, :autostart, false) do
        true ->
          delay = config(:startup_delay) |> ms()

          if delay > 0 do
            send_after(self(), {:startup}, delay)
            Map.put(s, :running, true)
          else
            s
          end

        false ->
          s
      end

    s =
      s
      |> Map.put_new(:led_flashes, 0)
      |> Map.put_new(:flash_led, config(:flash_led))
      |> Map.put_new(:periodic_log, config(:periodic_log))

    Logger.info(["init() state: ", inspect(s, pretty: true)])

    {:ok, s}
  end

  @manual_start_msg {:manual_start}
  def manual_start do
    GenServer.call(Mcp.SoakTest, @manual_start_msg)
  end

  @manual_stop_msg {:manual_stop}
  def manual_stop do
    GenServer.call(Mcp.SoakTest, @manual_stop_msg)
  end

  @running_check_msg {:running?}
  def running? do
    GenServer.call(Mcp.SoakTest, @running_check_msg)
  end

  def handle_call(@manual_start_msg, _from, s) do
    s =
      case s.running do
        true ->
          s

        false ->
          send_after(self(), {:startup}, 0)
          Map.put(s, :running, true)
      end

    {:reply, [], s}
  end

  def handle_call(@manual_stop_msg, _from, s) do
    s = Map.put(s, :running, false)

    {:reply, [], s}
  end

  def handle_call(@running_check_msg, _from, s) do
    {:reply, s.running, s}
  end

  # GenServer callbacks
  def handle_info({:flash_led}, s) do
    dev = "led1"
    led_flashes = s.led_flashes + 1

    Switch.Alias.position(dev, position: true)
    Switch.Alias.position(dev, position: false)

    LedFlashes.record(application: "mcp_soaktest", name: dev, val: led_flashes)

    s = %{s | led_flashes: led_flashes}

    case s.running do
      true -> send_after(self(), {:flash_led}, s.flash_led_ms)
      false -> nil
    end

    {:noreply, s}
  end

  def handle_info({:startup}, s)
      when is_map(s) do
    if config(:periodic_log_first) |> ms() > 0 do
      send_after(self(), {:periodic_log}, config(:periodic_log_first))
    end

    send_after(self(), {:flash_led}, ms(s.flash_led))

    Logger.info(["startup()"])

    {:noreply, s}
  end

  def handle_info({:periodic_log}, s)
      when is_map(s) do
    Logger.debug(["led flashes: ", inspect(s.led_flashes)])

    send_after(self(), {:periodic_log}, ms(s.periodic_log))

    {:noreply, s}
  end

  defp config(key)
       when is_atom(key) do
    get_env(:mcp, Mcp.SoakTest) |> Keyword.get(key)
  end
end
