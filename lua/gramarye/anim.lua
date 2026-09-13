local M = {}

local state = {}

function M.towards(id, target, rate)
    rate = rate or 12
    local now = gramarye.ui.time()
    local s = state[id]
    if not s then
        s = { value = target, t = now }
        state[id] = s
    end
    local dt = math.min(now - s.t, 0.1)
    s.t = now
    s.value = s.value + (target - s.value) * (1 - math.exp(-rate * dt))
    if math.abs(target - s.value) < 0.001 then s.value = target end
    return s.value
end

function M.value(id)
    local s = state[id]
    return s and s.value or 0
end

function M.reset(id)
    state[id] = nil
end

function M.lerp(a, b, t)
    return a + (b - a) * t
end

function M.lerp_color(a, b, t)
    return {
        math.floor(a[1] + (b[1] - a[1]) * t + 0.5),
        math.floor(a[2] + (b[2] - a[2]) * t + 0.5),
        math.floor(a[3] + (b[3] - a[3]) * t + 0.5),
        math.floor(a[4] + (b[4] - a[4]) * t + 0.5),
    }
end

return M
