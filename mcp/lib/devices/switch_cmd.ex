defmodule Mcp.SwitchCmd do
@moduledoc """
  The SwithCommand module provides the database schema for tracking
  commands sent for a Switch.
"""

require Logger
use Timex
use Timex.Ecto.Timestamps
use Ecto.Schema

# import Application, only: [get_env: 2]
import UUID, only: [uuid1: 0]
import Ecto.Changeset, only: [change: 2]
import Ecto.Query, only: [from: 2]
import Mcp.Repo, only: [one: 1, query: 1, preload: 2, insert!: 1, update!: 1]

import Mqtt.Client, only: [publish_switch_cmd: 1]

alias Command.SetSwitch
alias Mcp.SwitchState
alias Mcp.SwitchCmd

schema "switch_cmd" do
  field :refid, :string
  field :acked, :boolean
  field :dev_latency, :integer
  field :rt_latency, :integer
  field :sent_at, Timex.Ecto.DateTime
  field :ack_at, Timex.Ecto.DateTime
  belongs_to :switch, Mcp.Switch

  timestamps usec: true
end

def unacked_count, do: unacked_count([])
def unacked_count(opts)
when is_list(opts) do
  earlier = Timex.to_datetime(Timex.now(), "UTC") |>
              Timex.shift(minutes: -1)

  from(c in SwitchCmd,
    where: c.acked == false,
    where: c.sent_at < ^earlier,
    select: count(c.id)) |> one()
end

def purge_acked_cmds(opts)
when is_list(opts) do

  hrs_ago = Keyword.get(opts, :older_than_hrs, 12) * -1

  sql = ~s/delete from switch_cmd
              where ack_at <
              now() at time zone 'utc' - interval '#{hrs_ago} hour'/

  query(sql) |> check_purge_acked_cmds()
end

def record_cmd([%SwitchState{} = ss_ref | _tail] = list) do
  ss_ref = preload(ss_ref, :switch)  # ensure the associated switch is loaded
  sw = ss_ref.switch
  device = sw.device

  # create and presist a new switch comamnd
  scmd =
    Ecto.build_assoc(sw, :cmds,
                      refid: uuid1(),
                      sent_at: Timex.now()) |> insert!()

  # update the last command datetime on the associated switch
  change(sw, dt_last_cmd: Timex.now()) |> update!()

  # create and publish the actual command to the remote device
  new_state = SwitchState.as_list_of_maps(list)
  remote_cmd = SetSwitch.new_cmd(device, new_state, scmd.refid)
  publish_switch_cmd(SetSwitch.json(remote_cmd))

  list # return the switch states passed in
end
#
# Private functions
#

defp check_purge_acked_cmds({:error, e}) do
  Logger.warn fn ->
    ~s/failed to purge acked cmds msg='#{Exception.message(e)}'/ end
  0
end

defp check_purge_acked_cmds({:ok, %{command: :delete, num_rows: nr}}), do: nr

end
