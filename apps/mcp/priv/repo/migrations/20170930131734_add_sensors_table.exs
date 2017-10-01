defmodule Mcp.Repo.Migrations.AddSensorsTable do
  @moduledoc """
  """
  use Ecto.Migration

  def change do
    current_time = fragment(~s/(now() at time zone 'utc')/)
    # before_now = fragment(~s/now() at time zone 'utc' - interval '3 month')/)
    before_now = fragment(~s/(now() at time zone 'utc' - interval '3 hour')/)

    drop_if_exists table(:switches)
    drop_if_exists index(:switches, [:friendly_name])
    create_if_not_exists table(:switches) do
      add :friendly_name, :string, size: 20, null: false
      add :disabled, :boolean, null: false, default: false
      add :pio, :boolean, null: false, default: false
      add :num_pio, :integer, null: false, default: 0
      add :pios, :map, null: false, default: %{}
      add :pending_cmds, {:array, :map}, null: false, default: []
      add :dt_last_cmd, :utc_datetime, default: before_now
      add :dt_discovered, :utc_datetime, default: current_time

      timestamps()
    end

    create_if_not_exists index(:switches, [:friendly_name], unique: true)
  end
end
