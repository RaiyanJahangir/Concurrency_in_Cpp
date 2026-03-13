-- Generate requests for the mixed /work benchmark.
--
-- Usage example:
--   wrk -t4 -c32 -d10s --latency \
--     -s wrk_workload.lua http://127.0.0.1:8080 -- 200 5000 200

local path = "/work?cpu1=200&io=5000&cpu2=200"

init = function(args)
  local cpu1 = tonumber(args[1]) or 200
  local io = tonumber(args[2]) or 5000
  local cpu2 = tonumber(args[3]) or 200

  path = string.format("/work?cpu1=%d&io=%d&cpu2=%d", cpu1, io, cpu2)
end

request = function()
  return wrk.format("GET", path, {
    ["Host"] = wrk.headers["Host"] or "localhost",
    ["Connection"] = "keep-alive"
  })
end
