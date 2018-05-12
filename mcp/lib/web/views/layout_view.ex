defmodule Web.LayoutView do
  use Web, :view

  require Logger

  def locate_ui_files do
    bundles_path = Application.app_dir(:mcp, "priv/static/bundles")

    {:ok, files} = File.ls(bundles_path)

    # styles_re = ~r/(?<file>styles\.[[:xdigit]]+\.bundle\.css)/
    styles_re = ~r/^styles\.[[:xdigit:]]+\.bundle\.css$/
    js_re = ~r/^[a-z]+\.[[:xdigit:]]+\.bundle\.js$/

    ss = for f <- files, Regex.match?(styles_re, f), do: f

    js =
      for f <- files, Regex.match?(js_re, f) do
        Logger.warn(fn -> "found js file: #{f}" end)
        f
      end

    Logger.warn(fn -> inspect(ss, pretty: true) end)

    %{ss: ss, js: js}
  end

  def ui_js_files do
    # files = ["inline", "polyfills", "styles", "vendor", "main"]
    files = ["inline", "polyfills", "main"]
    # env = "#{Mix.env()}"

    # if env === "dev" or env === "test" do
    #   for f <- files, do: "#{f}.bundle.js"
    # else
    %{ss: _, js: js_files} = locate_ui_files()

    for f <- files do
      Enum.find(js_files, fn x -> String.contains?(x, f) end)
    end

    # end
  end

  def stylesheets do
    locate_ui_files() |> Map.get(:ss, [])
  end

  def bundle_file(conn, file) do
    static_path(conn, "/bundles/#{file}")
  end
end
