-- Neon Pong for TrayPlay (320x180 Virtual Console)

local paddle_w = 4
local paddle_h = 28
local paddle_speed = 3

local p1 = { x = 12, y = 76, score = 0 }
local p2 = { x = 304, y = 76, score = 0 }

local ball = {
    x = 160,
    y = 90,
    vx = 2.5,
    vy = 1.5,
    size = 4
}

function init()
    p1.y = 76
    p2.y = 76
    p1.score = 0
    p2.score = 0
    reset_ball(1)
end

function reset_ball(dir)
    ball.x = 160
    ball.y = 90
    ball.vx = 2.5 * dir
    ball.vy = (math.random() > 0.5 and 1.5 or -1.5)
end

function update(dt)
    -- 1. Player 1 input (Arrow keys / W/S or auto demo)
    if btn("up") or btn("w") then
        p1.y = math.max(4, p1.y - paddle_speed)
    elseif btn("down") or btn("s") then
        p1.y = math.min(180 - paddle_h - 4, p1.y + paddle_speed)
    else
        -- Simple auto-assist demo mode if no buttons held
        if ball.vx < 0 then
            if p1.y + paddle_h / 2 < ball.y - 2 then
                p1.y = math.min(180 - paddle_h - 4, p1.y + paddle_speed * 0.7)
            elseif p1.y + paddle_h / 2 > ball.y + 2 then
                p1.y = math.max(4, p1.y - paddle_speed * 0.7)
            end
        end
    end

    -- 2. AI Player 2 tracking
    local ai_center = p2.y + paddle_h / 2
    if ball.vx > 0 and ball.x > 100 then
        if ai_center < ball.y - 3 then
            p2.y = math.min(180 - paddle_h - 4, p2.y + paddle_speed * 0.85)
        elseif ai_center > ball.y + 3 then
            p2.y = math.max(4, p2.y - paddle_speed * 0.85)
        end
    end

    -- 3. Ball movement
    ball.x = ball.x + ball.vx
    ball.y = ball.y + ball.vy

    -- 4. Bounce top and bottom boundaries
    if ball.y <= 4 then
        ball.y = 4
        ball.vy = -ball.vy
    elseif ball.y >= 180 - ball.size - 4 then
        ball.y = 180 - ball.size - 4
        ball.vy = -ball.vy
    end

    -- 5. Collision with Left Paddle (p1)
    if ball.x <= p1.x + paddle_w and ball.x + ball.size >= p1.x then
        if ball.y + ball.size >= p1.y and ball.y <= p1.y + paddle_h then
            ball.x = p1.x + paddle_w + 1
            ball.vx = math.abs(ball.vx) * 1.05 -- speed up
            -- Angle deflection based on hit position
            local offset = (ball.y + ball.size / 2) - (p1.y + paddle_h / 2)
            ball.vy = offset * 0.15
        end
    end

    -- 6. Collision with Right Paddle (p2)
    if ball.x + ball.size >= p2.x and ball.x <= p2.x + paddle_w then
        if ball.y + ball.size >= p2.y and ball.y <= p2.y + paddle_h then
            ball.x = p2.x - ball.size - 1
            ball.vx = -math.abs(ball.vx) * 1.05 -- speed up
            local offset = (ball.y + ball.size / 2) - (p2.y + paddle_h / 2)
            ball.vy = offset * 0.15
        end
    end

    -- 7. Goal detection
    if ball.x < 0 then
        p2.score = p2.score + 1
        reset_ball(1)
    elseif ball.x > 320 then
        p1.score = p1.score + 1
        reset_ball(-1)
    end
end

function draw()
    -- Clear to dark blue (retro palette 1)
    cls(1)

    -- Field borders (top & bottom)
    rectfill(0, 0, 320, 3, 5) -- dark gray border
    rectfill(0, 177, 320, 3, 5)

    -- Center dividing net
    for y = 6, 174, 8 do
        rectfill(159, y, 2, 4, 5)
    end

    -- Left Paddle: Blue (palette 12)
    rectfill(p1.x, p1.y, paddle_w, paddle_h, 12)

    -- Right Paddle: Red (palette 8)
    rectfill(p2.x, p2.y, paddle_w, paddle_h, 8)

    -- Ball: Yellow (palette 10)
    rectfill(math.floor(ball.x), math.floor(ball.y), ball.size, ball.size, 10)

    -- Score indicators (simple bar graphs)
    for i = 1, math.min(10, p1.score) do
        rectfill(140 - (i * 6), 8, 4, 4, 12)
    end
    for i = 1, math.min(10, p2.score) do
        rectfill(176 + (i * 6), 8, 4, 4, 8)
    end
end
