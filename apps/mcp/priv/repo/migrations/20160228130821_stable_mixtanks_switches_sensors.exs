defmodule Mcp.Repo.Migrations.StableMixtanksSwitchesSensors do
  use Ecto.Migration

  def change do
    drop_if_exists table(:sensors)
    drop_if_exists index(:sensors, [:name, :reading])
    create_if_not_exists table(:sensors) do
      add :name, :string, size: 25, null: false
      add :reading, :string, size: 25, null: false
      add :description, :text
      add :value, :float, null: false, default: 0.0
      add :read_at, :datetime, default: fragment(~s/(now() at time zone 'utc')/) 

      timestamps
    end
    create_if_not_exists index(:sensors, [:name, :reading], unique: true)

    drop_if_exists table(:switches)
    drop_if_exists index(:switches, [:name])
    create_if_not_exists table(:switches) do
      add :name, :string, size: 25, null: false
      add :description, :text
      add :group, :string, size: 25, null: false
      add :pio, :string, size: 6, null: false
      add :state, :boolean
      add :state_at, :datetime, default: fragment(~s/(now() at time zone 'utc')/)
      
      timestamps    
    end
    create_if_not_exists index(:switches, [:name], unique: true)

    drop_if_exists table(:mixtanks)
    drop_if_exists index(:mixtanks, [:name])
    create table(:mixtanks) do
      add :name, :string, size: 25, null: false
      add :description, :text
      add :enable, :boolean, null: false
      add :sensor, :string, size: 25, null: false
      add :ref_sensor, :string, size: 25, null: false
      add :heat_sw, :string, size: 25, null: false
      add :heat_state, :boolean, null: false, default: false
      add :air_sw, :string, size: 25, null: false
      add :air_state, :boolean, null: false, default: false
      add :air_run_ms, :integer, null: false, default: 0
      add :air_idle_ms, :integer, null: false, default: 0
      add :pump_sw, :string, size: 25, null: false
      add :pump_state, :boolean, null: false, default: false
      add :pump_run_ms, :integer, null: false, default: 0
      add :pump_idle_ms, :integer, null: false, default: 0
      add :state_at, :datetime, default: fragment(~s/(now() at time zone 'utc')/)
      
      timestamps
    end
    create index(:mixtanks, [:name], unique: true)

  end
end