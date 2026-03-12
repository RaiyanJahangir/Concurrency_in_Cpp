-- Generate requests for the mixed /work benchmark.
--
-- Usage example:
--   wrk -t4 -c32 -d10s --latency \
--     -s wrk_workload.lua http://127.0.0.1:8080 -- 2 5000 2 1024 64

local path = "/work?cpu1_mm=2&io=5000&cpu2_mm=2&mm_n=1024&mm_bs=64"

init = function(args)
  local cpu1_mm = tonumber(args[1]) or 2
  local io = tonumber(args[2]) or 5000
  local cpu2_mm = tonumber(args[3]) or 2
  local mm_n = tonumber(args[4]) or 1024
  local mm_bs = tonumber(args[5]) or 64

  path = string.format(
    "/work?cpu1_mm=%d&io=%d&cpu2_mm=%d&mm_n=%d&mm_bs=%d",
    cpu1_mm, io, cpu2_mm, mm_n, mm_bs
  )
end

request = function()
  return wrk.format("GET", path, {
    ["Host"] = wrk.headers["Host"] or "localhost",
    ["Connection"] = "keep-alive"
  })
end
